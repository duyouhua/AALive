//
// Created on 2017/10/24.
// Class for converting OES textures to a YUV ByteBuffer. It should be constructed on a thread with
// an active EGL context, and only be used from that thread.
#include "YuvConverter.h"
#include "ProgramShaderUtil.h"
#include "Logger.h"
#include "GLUtil.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES2/gl2platform.h>
  // Vertex coordinates in Normalized Device Coordinates, i.e.
  // (-1, -1) is bottom-left and (1, 1) is top-right.
  const static GLfloat DEVICE_RECTANGLE[] = {
      -1.0f, -1.0f, // Bottom left.
      1.0f, -1.0f, // Bottom right.
      -1.0f, 1.0f, // Top left.
      1.0f, 1.0f, // Top right.
  };

  // Texture coordinates - (0, 0) is bottom-left and (1, 1) is top-right.
  const static GLfloat TEXTURE_RECTANGLE[] = {
      0.0f, 0.0f, // Bottom left.
      1.0f, 0.0f, // Bottom right.
      0.0f, 1.0f, // Top left.
      1.0f, 1.0f // Top right.
  };

  // clang-format off
  const static char* VERTEX_SHADER =
        "varying vec2 interp_tc;\n"
        "attribute vec4 in_pos;\n"
        "attribute vec4 in_tc;\n"
        "void main() {\n"
        "    gl_Position = in_pos;\n"
        "    interp_tc = in_tc.xy;\n"
        "}\n";

  const static char* FRAGMENT_SHADER =
      "precision mediump float;\n"
      "varying vec2 interp_tc;\n"
       "uniform sampler2D oesTex;\n"
       "uniform vec4 coeffs;\n"       // Color conversion coefficients, including constant term
       "void main() {\n"
      // Since the alpha read from the texture is always 1, this could
      // be written as a mat4 x vec4 multiply. However, that seems to
      // give a worse framerate, possibly because the additional
      // multiplies by 1.0 consume resources. TODO(nisse): Could also
      // try to do it as a vec3 x mat3x4, followed by an add in of a
      // constant vector.
        "  gl_FragColor.r = coeffs.a + dot(coeffs.rgb,\n"
        "      texture2D(oesTex, interp_tc).rgb);\n"
        "  gl_FragColor.g = coeffs.a + dot(coeffs.rgb,\n"
        "      texture2D(oesTex, interp_tc).rgb);\n"
        "  gl_FragColor.b = coeffs.a + dot(coeffs.rgb,\n"
        "      texture2D(oesTex, interp_tc).rgb);\n"
        "  gl_FragColor.a = coeffs.a + dot(coeffs.rgb,\n"
        "      texture2D(oesTex, interp_tc).rgb);\n"
        "}\n";

YuvConverter::YuvConverter():mShader(NULL),mFramebuffer(NULL),mTexture2d(NULL){
    mShader = new Shader;
    if(!mShader){
        LOGE("out of memory func: %s, line: %d",__FUNCTION__, __LINE__);
        return;
    }
    mFramebuffer = new Framebuffer;
    if(mFramebuffer){
        GLUtil::checkGLError("YuvConverter() create framebuffer before");
        LOGE("YuvConverter() create framebuffer before: %s, line: %d",__FUNCTION__, __LINE__);
        mFramebuffer->create();
        GLUtil::checkGLError("YuvConverter() create framebuffer");
        mShader->create(VERTEX_SHADER, FRAGMENT_SHADER);
        mShader->useProgram();
        vertexAttribLoc = mShader->getAttribLocation ("in_pos");
        GLUtil::checkGLError("YuvConverter() GetAttribLocation in_pos");

        textureAttribLoc = mShader->getAttribLocation ("in_tc");
        GLUtil::checkGLError("YuvConverter() GetAttribLocation in_tc");

        textureUniformLoc = mShader->getUniformLocation ("oesTex");
        GLUtil::checkGLError("YuvConverter() GetUniformLocation oesTex");

        coeffsUniformLoc = mShader->getUniformLocation ("coeffs");
        GLUtil::checkGLError("YuvConverter() GetUniformLocation coeffs");
    }else{
        release();
        LOGE("out of memory func: %s, line: %d",__FUNCTION__, __LINE__);
    }
}

YuvConverter::~YuvConverter(){
    release();
}

void YuvConverter::release(){
        if(mFramebuffer){
            delete mFramebuffer;
            mFramebuffer = NULL;
         }
         if(mShader){
             delete mShader;
             mShader = NULL;
         }
         if(mTexture2d){
            delete mTexture2d;
            mTexture2d = NULL;
         }
}

bool YuvConverter::convert(void *buf, int width, int height, int srcTextureId){
    if(!mShader || !mFramebuffer){
        return false;
    }
        // We draw into a buffer laid out like
        //
        //    +---------+
        //    |         |
        //    |  Y      |
        //    |         |
        //    |         |
        //    +----+----+
        //    | U  | V  |
        //    |    |    |
        //    +----+----+
        //
        // In memory, we use the same stride for all of Y, U and V. The
        // U data starts at offset |height| * |stride| from the Y data,
        // and the V data starts at at offset |stride/2| from the U
        // data, with rows of U and V data alternating.
        //
        // Now, it would have made sense to allocate a pixel buffer with
        // a single byte per pixel (EGL10.EGL_COLOR_BUFFER_TYPE,
        // EGL10.EGL_LUMINANCE_BUFFER,), but that seems to be
        // unsupported by devices. So do the following hack: Allocate an
        // RGBA buffer, of width |stride|/4. To render each of these
        // large pixels, sample the texture at 4 different x coordinates
        // and store the results in the four components.
        //
        // Since the V data needs to start on a boundary of such a
        // larger pixel, it is not sufficient that |stride| is even, it
        // has to be a multiple of 8 pixels.
    int y_width = (width + 3) / 4;
    int uv_width = (width + 7) / 8;
    int uv_height = (height + 1) / 2;
    int total_height = height + uv_height;
    int frameBufferWidth = width / 4;
    int frameBufferHeight = total_height;

    mShader->useProgram();

    if(!mTexture2d){
        mTexture2d = new Texture2d;
        if(!mTexture2d){
            LOGE("out of memory func: %s, line: %d",__FUNCTION__, __LINE__);
            return false;
        }
        mTexture2d->genTexture((GLsizei)frameBufferWidth, (GLsizei)frameBufferHeight, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        GLUtil::checkGLError("YuvConverter::convert create texture");
    }

    glEnableVertexAttribArray(vertexAttribLoc);
    glVertexAttribPointer(vertexAttribLoc, 2, GL_FLOAT, false, 0, DEVICE_RECTANGLE);
    glEnableVertexAttribArray(textureAttribLoc);
    glVertexAttribPointer(textureAttribLoc, 2, GL_FLOAT, false, 0, TEXTURE_RECTANGLE);

    mFramebuffer->bindTexture(GL_TEXTURE_2D, GL_TEXTURE2, mTexture2d->getTextureId());

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, srcTextureId);
    glUniform1i(textureUniformLoc, GL_TEXTURE3 - GL_TEXTURE0);

    // Draw Y
    glViewport(0, 0, y_width, height);
    // Y'UV444 to RGB888, see
    // https://en.wikipedia.org/wiki/YUV#Y.27UV444_to_RGB888_conversion.
    // We use the ITU-R coefficients for U and V */
    glUniform4f(coeffsUniformLoc, 0.299f, 0.587f, 0.114f, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Draw U
    glViewport(0, height, uv_width, uv_height);
    glUniform4f(coeffsUniformLoc, -0.169f, -0.331f, 0.499f, 0.5f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Draw V
    glViewport(uv_width, height, uv_width, uv_height);
    glUniform4f(coeffsUniformLoc, 0.499f, -0.418f, -0.0813f, 0.5f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glReadPixels(0, 0, mTexture2d->getWidth(), mTexture2d->getHeight(), GL_RGBA, GL_UNSIGNED_BYTE, buf);

    // Restore normal framebuffer.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    mFramebuffer->unbindTexture(GL_TEXTURE_2D, GL_TEXTURE2);

    //glDisableVertexAttribArray(vertexAttribLoc);
    //glDisableVertexAttribArray(textureAttribLoc);
    return true;
}

