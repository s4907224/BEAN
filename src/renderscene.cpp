#include "renderscene.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <ngl/Obj.h>
#include <ngl/NGLInit.h>
#include <ngl/VAOPrimitives.h>
#include <ngl/ShaderLib.h>
#include <iomanip>

RenderScene::RenderScene() : m_width(1),
                             m_height(1),
                             m_ratio(1.0f),
                             m_crowdSim (new CrowdSim)
{
  m_crowdSim->calculateRoutes();
  m_startTime = std::chrono::high_resolution_clock::now();
  m_prevFrameTime = std::chrono::high_resolution_clock::now();
  float maxLightDist = 0;
  for (size_t i = 0; i < m_lightPos.size(); i++)
  {
    maxLightDist = std::max(glm::length(m_lightPos[i]), maxLightDist);
  }
  float multiplier = 256.f / maxLightDist;
  for (size_t i = 0; i < m_lightPos.size(); i++)
  {
    m_lightPos[i] *= multiplier;
  }
}

RenderScene::~RenderScene() = default;

void RenderScene::resizeGL(GLint _width, GLint _height) noexcept
{
  m_width = _width;
  m_height = _height;
  m_ratio = m_width / float(m_height);
  m_isFBODirty = true;
  m_pixelSizeScreenSpace.x = 1.f / m_width;
  m_pixelSizeScreenSpace.y = 1.f / m_height;
  updateJitter();
}

void RenderScene::initGL(std::string mapImage) noexcept
{
  ngl::NGLInit::instance();
  glClearColor(0.8f, 0.8f, 0.8f, 1.f);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  std::string mapName;
  for (auto &character : mapImage)
  {
    if (character == '.') {break;}
    mapName.push_back(character);
  }
  m_peepMesh = new ngl::Obj("models/peepmesh.obj");
  m_peepMesh->createVAO();

  m_floorMesh = new ngl::Obj(std::string("models/") + mapName + std::string(".obj"));
  m_floorMesh->createVAO();

  ngl::ShaderLib *shader=ngl::ShaderLib::instance();

  shader->loadShader("environmentShader",
                     "shaders/env_v.glsl",
                     "shaders/env_f.glsl");

  shader->loadShader("phongShader",
                     "shaders/phong_v.glsl",
                     "shaders/phong_f.glsl");

  shader->loadShader("beckmannShader",
                     "shaders/beckmann_v.glsl",
                     "shaders/beckmann_f.glsl");

  shader->loadShader("taaShader",
                     "shaders/taa_v.glsl",
                     "shaders/taa_f.glsl");

  shader->loadShader("blitShader",
                     "shaders/blit_v.glsl",
                     "shaders/blit_f.glsl");

  shader->use("beckmannShader");
  GLuint shaderID = shader->getProgramID("beckmannShader");
  initTexture(taa_floorTex, m_floorTex, std::string("images/" + mapImage).c_str());

  glUniform3fv(glGetUniformLocation(shaderID, "lightPos"),
               int(m_lightCol.size()),
               glm::value_ptr(m_lightPos[0]));

  ngl::VAOPrimitives *prim = ngl::VAOPrimitives::instance();
  prim->createTrianglePlane("plane",2,2,1,1,ngl::Vec3(0,1,0));
  m_pixelSizeScreenSpace.x = 1.f / m_width;
  m_pixelSizeScreenSpace.y = 1.f / m_height;
}

void RenderScene::paintGL(bool paused) noexcept
{
  if (!paused) {m_crowdSim->update();}
  static int count = 0;
  //Common stuff
  if (m_isFBODirty)
  {
    initFBO(m_renderFBO, m_renderFBOColour, m_renderFBODepth);
    initFBO(m_aaFBO1, m_aaFBOColour1, m_aaFBODepth1);
    initFBO(m_aaFBO2, m_aaFBOColour2, m_aaFBODepth2);
    m_aaDirty = true;
    m_isFBODirty = false;
  }

  size_t activeAAFBO;
  if (m_flip) {activeAAFBO = m_aaFBO1;}
  else        {activeAAFBO = m_aaFBO2;}

  //Scene
  renderScene(activeAAFBO);


  if (m_activeAA != msaa)
  {
    //AA
    if (!m_aaDirty && m_activeAA == taa) {antialias(activeAAFBO);}

    //Blit
    if (m_flip)
    {
      blit(m_aaFBO1, m_aaFBOColour1, m_aaColourTU1);
//      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
//      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_aaFBOColour1);
//      glDrawBuffer(GL_BACK);
//      glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    else
    {
      blit(m_aaFBO2, m_aaFBOColour2, m_aaColourTU2);
//      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
//      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_aaFBOColour2);
//      glDrawBuffer(GL_BACK);
//      glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    //Cycle jitter
    m_jitterCounter++;
    if (m_jitterCounter > (m_sampleVector.size() - 1)) {m_jitterCounter = 0;}

    m_aaDirty = false;
    m_flip = !m_flip;
  }

  //Calculate fps
  auto now = std::chrono::high_resolution_clock::now();
  int elapsedSeconds = int(std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count());
  static bool startSecond = true;
  if (elapsedSeconds % 2 == 1)
  {
    if (startSecond)
    {
      double fps = count / 2.0;
      std::cout<<fps<<'\t'<<" FPS\n";
      startSecond = false;
      count = 0;
    }
  }
  else {startSecond = true;}
  count++;
}

void RenderScene::antialias(size_t _activeAAFBO)
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_arrFBO[_activeAAFBO][taa_fboID]);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0,0,m_width,m_height);

  ngl::ShaderLib* shader = ngl::ShaderLib::instance();
  ngl::VAOPrimitives* prim = ngl::VAOPrimitives::instance();
  glm::mat4 screenMVP = glm::rotate(glm::mat4(1.0f), glm::pi<float>() * 0.5f, glm::vec3(1.0f,0.0f,0.0f));

  shader->use("taaShader");
  GLuint shaderID = shader->getProgramID("taaShader");

  glm::mat4 inverseVPC = glm::inverse(m_VP);

  glActiveTexture(m_renderFBOColour);
  glBindTexture(GL_TEXTURE_2D, m_arrFBO[m_renderFBO][taa_fboTextureID]);
  glActiveTexture(m_renderFBODepth);
  glBindTexture(GL_TEXTURE_2D, m_arrFBO[m_renderFBO][taa_fboDepthID]);
  //Bind the inactive aaFBO
  if (_activeAAFBO == m_aaFBO1)
  {
    glActiveTexture(m_aaFBOColour2);
    glBindTexture(GL_TEXTURE_2D, m_arrFBO[m_aaFBO2][taa_fboTextureID]);
  }
  else
  {
    glActiveTexture(m_aaFBOColour1);
    glBindTexture(GL_TEXTURE_2D, m_arrFBO[m_aaFBO1][taa_fboTextureID]);
  }

  glUniform1i(glGetUniformLocation(shaderID, "colourRENDER"),       m_renderColourTU);
  glUniform1i(glGetUniformLocation(shaderID, "depthRENDER"),        m_renderDepthTU);
  if (_activeAAFBO == m_aaFBO1) {glUniform1i(glGetUniformLocation(shaderID, "colourANTIALIASED"),  m_aaColourTU2);}
  else                          {glUniform1i(glGetUniformLocation(shaderID, "colourANTIALIASED"),  m_aaColourTU1);}
  glUniform2f(glGetUniformLocation(shaderID, "windowSize"),         m_width, m_height);
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "inverseViewProjectionCURRENT"),
                     1,
                     false,
                     glm::value_ptr(inverseVPC));
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "VPCURRENT"),
                     1,
                     false,
                     glm::value_ptr(m_VP));
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "viewProjectionHISTORY"),
                     1,
                     false,
                     glm::value_ptr(m_lastVP));
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "MVP"),
                     1,
                     false,
                     glm::value_ptr(screenMVP));
  glm::vec2 screenSpaceJitter = m_jitterVector[m_jitterCounter] * - 0.5f;
  glUniform2fv(glGetUniformLocation(shaderID, "jitter"),
               1,
               glm::value_ptr(screenSpaceJitter));
  glUniform1f(glGetUniformLocation(shaderID, "feedback"), m_feedback);
  glUniform2fv(glGetUniformLocation(shaderID, "pixelSize"),
               1,
               glm::value_ptr(m_pixelSizeScreenSpace));
  prim->draw("plane");
}

void RenderScene::blit(size_t _fbo, GLenum _texture, int _textureUnit)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0,0,m_width,m_height);

  ngl::ShaderLib* shader = ngl::ShaderLib::instance();
  ngl::VAOPrimitives* prim = ngl::VAOPrimitives::instance();
  glm::mat4 screenMVP = glm::rotate(glm::mat4(1.0f), glm::pi<float>() * 0.5f, glm::vec3(1.0f,0.0f,0.0f));

  shader->use("blitShader");
  GLuint shaderID = shader->getProgramID("blitShader");

  glActiveTexture(_texture);
  glBindTexture(GL_TEXTURE_2D, m_arrFBO[_fbo][taa_fboTextureID]);

  glUniform1i(glGetUniformLocation(shaderID, "inputTex"), _textureUnit);
  glUniform2f(glGetUniformLocation(shaderID, "windowSize"), m_width, m_height);
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "MVP"),
                     1,
                     false,
                     glm::value_ptr(screenMVP));
  prim->draw("plane");
}

void RenderScene::renderScene(size_t _activeAAFBO)
{
  if (m_aaDirty || m_activeAA == none) {glBindFramebuffer(GL_FRAMEBUFFER, m_arrFBO[_activeAAFBO][taa_fboID]);}
  else if (m_activeAA == taa)          {glBindFramebuffer(GL_FRAMEBUFFER, m_arrFBO[m_renderFBO][taa_fboID]);}
  else                                 {glBindFramebuffer(GL_FRAMEBUFFER, 0);}
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0,0,m_width,m_height);
  ngl::ShaderLib* shader = ngl::ShaderLib::instance();
  GLuint shaderID = shader->getProgramID("beckmannShader");
  shader->use("beckmannShader");

  m_lastVP = m_VP;
  glm::mat4 jitterMatrix;
  if (m_activeAA == taa)
  {
    glm::vec3 help {m_jitterVector[m_jitterCounter].x, m_jitterVector[m_jitterCounter].y, 0.f};
    jitterMatrix = glm::translate(jitterMatrix, help);
  }
  //Jitter the VP
  m_VP = jitterMatrix * m_proj * m_view;
  //Use it to calculate the jittered MVP
  glUniform1i(glGetUniformLocation(shaderID, "envMapMaxLod"), 10);
  glUniform3fv(glGetUniformLocation(shaderID, "cameraPos"),
               1,
               glm::value_ptr(m_cameraPos));
  if (m_activeAA == taa)
  {
    glm::vec2 jit = m_jitterVector[m_jitterCounter] * - 0.5f;
    glUniform2fv(glGetUniformLocation(shaderID, "jitter"),
                 1,
                 glm::value_ptr(jit));
  }
  else
  {
    glUniform2f(glGetUniformLocation(shaderID, "jitter"), 0.f, 0.f);
  }

  for (auto &obj : m_crowdSim->getPeeps())
  {
    glm::mat4 M, MV, MVP;
    glm::mat3 N;
    M = glm::mat4(1.f);
    //M = glm::rotate(M, glm::pi<float>() * 0.25f, {0.f, 1.f, 0.f});
    glm::vec2 pos2D = obj->getPosition();
    M = glm::translate(M, {pos2D.x, 0.f, pos2D.y});
    MV = m_view * M;
    MVP = m_VP * M;
    //Remove the jitter
    N = glm::inverse(glm::mat3(M));

    glUniformMatrix4fv(glGetUniformLocation(shaderID, "MV"),
                       1,
                       false,
                       glm::value_ptr(MV));
    glUniformMatrix4fv(glGetUniformLocation(shaderID, "MVP"),
                       1,
                       false,
                       glm::value_ptr(MVP));
    glUniformMatrix3fv(glGetUniformLocation(shaderID, "N"),
                       1,
                       true,
                       glm::value_ptr(N));
    glUniform1f(glGetUniformLocation(shaderID, "roughness"), obj->getShaderProps()->m_roughness);
    glUniform1f(glGetUniformLocation(shaderID, "metallic"), obj->getShaderProps()->m_metallic);
    glUniform1f(glGetUniformLocation(shaderID, "diffAmount"), obj->getShaderProps()->m_diffuseWeight);
    glUniform1f(glGetUniformLocation(shaderID, "specAmount"), obj->getShaderProps()->m_specularWeight);
    glUniform3fv(glGetUniformLocation(shaderID, "materialDiff"),
                 1,
                 glm::value_ptr(obj->getShaderProps()->m_diffuseColour));
    glUniform3fv(glGetUniformLocation(shaderID, "materialSpec"),
                 1,
                 glm::value_ptr(obj->getShaderProps()->m_specularColour));
    glUniform1f(glGetUniformLocation(shaderID, "alpha"), obj->getShaderProps()->m_alpha);
    glUniform1i(glGetUniformLocation(shaderID, "hasDiffMap"), 0);
    m_peepMesh->draw();
  }
  glm::mat4 M, MV, MVP;
  glm::mat3 N;
  M = glm::mat4(1.f);
  M = glm::translate(M, {127.5f, 0.f, 127.5});
  M = glm::rotate(M, glm::pi<float>() * -0.5f, glm::vec3(0.f, 1.f, 0.f));
  MV = m_view * M;
  MVP = m_VP * M;
  //Remove the jitter
  N = glm::inverse(glm::mat3(M));

  glUniformMatrix4fv(glGetUniformLocation(shaderID, "MV"),
                     1,
                     false,
                     glm::value_ptr(MV));
  glUniformMatrix4fv(glGetUniformLocation(shaderID, "MVP"),
                     1,
                     false,
                     glm::value_ptr(MVP));
  glUniformMatrix3fv(glGetUniformLocation(shaderID, "N"),
                     1,
                     true,
                     glm::value_ptr(N));
  glUniform1f(glGetUniformLocation(shaderID, "roughness"), 1.f);
  glUniform1f(glGetUniformLocation(shaderID, "metallic"), 0.f);
  glUniform1f(glGetUniformLocation(shaderID, "diffAmount"), 2.f);
  glUniform1f(glGetUniformLocation(shaderID, "specAmount"), 0.f);
  glUniform3fv(glGetUniformLocation(shaderID, "materialDiff"),
               1,
               glm::value_ptr(glm::vec3(1.f, 1.f, 1.f)));
  glUniform3fv(glGetUniformLocation(shaderID, "materialSpec"),
               1,
               glm::value_ptr(glm::vec3(1.f, 1.f, 1.f)));
  glUniform1i(glGetUniformLocation(shaderID, "hasDiffMap"), 1);
  glUniform1i(glGetUniformLocation(shaderID, "diffuseMap"), taa_floorTex);
  m_floorMesh->draw();
  m_VP = m_proj * m_view;
}

void RenderScene::setViewMatrix(glm::mat4 _view)
{
  m_lastView = m_view;
  m_view = _view;
}

void RenderScene::setProjMatrix(glm::mat4 _proj)
{
  m_lastProj = m_proj;
  m_proj = _proj;
}

void RenderScene::setCubeMatrix(glm::mat4 _cube)
{
  m_cube = _cube;
}

void RenderScene::setCameraLocation(glm::vec3 _location)
{
  m_cameraPos = _location;
}

void RenderScene::setAAMethod(int _method)
{
  m_activeAA = _method;
}

void RenderScene::resetTAA()
{
  m_aaDirty = true;
}

void RenderScene::initFBO(size_t _fboID, GLenum _textureA, GLenum _textureB)
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_arrFBO[_fboID][taa_fboID]);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == (GL_FRAMEBUFFER_COMPLETE))
  {
   glDeleteTextures(1, &m_arrFBO[_fboID][taa_fboTextureID]);
   glDeleteTextures(1, &m_arrFBO[_fboID][taa_fboDepthID]);
   glDeleteFramebuffers(1, &m_arrFBO[_fboID][taa_fboID]);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glGenTextures(1, &m_arrFBO[_fboID][taa_fboTextureID]);
  glActiveTexture(_textureA);
  glBindTexture(GL_TEXTURE_2D, m_arrFBO[_fboID][taa_fboTextureID]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glGenTextures(1, &m_arrFBO[_fboID][taa_fboDepthID]);
  glActiveTexture(_textureB);
  glBindTexture(GL_TEXTURE_2D, m_arrFBO[_fboID][taa_fboDepthID]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glGenFramebuffers(1, &m_arrFBO[_fboID][taa_fboID]);
  glBindFramebuffer(GL_FRAMEBUFFER, m_arrFBO[_fboID][taa_fboID]);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_arrFBO[_fboID][taa_fboTextureID], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_arrFBO[_fboID][taa_fboDepthID], 0);

  GLenum drawBufs[] = {GL_COLOR_ATTACHMENT0};
  glDrawBuffers(1, drawBufs);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {std::cout<<"This shouldn't appear (bug with Temporal Anti Aliasing)\n";}
}

void RenderScene::initTexture(const GLuint& texUnit, GLuint &texId, const char *filename)
{
    glActiveTexture(GL_TEXTURE0 + texUnit);

    ngl::Image img(filename);

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    glTexImage2D (
                GL_TEXTURE_2D,
                0,
                int(img.format()),
                int(img.width()),
                int(img.height()),
                0,
                img.format(),
                GL_UNSIGNED_BYTE,
                img.getPixels());

    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void RenderScene::updateJitter()
{
  for (size_t i = 0; i < m_jitterVector.size(); i++){m_jitterVector[i] = m_sampleVector[i] * m_pixelSizeScreenSpace * 0.9f;}
}

void RenderScene::increaseFeedback(float _delta)
{
  m_feedback += _delta;
  if (m_feedback > 1.f) {m_feedback = 1.f;}
  if (m_feedback < 0.f) {m_feedback = 0.f;}
}
