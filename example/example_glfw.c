//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#include <stdio.h>
#include "platform.h"
#include <GLFW/glfw3.h>
#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
#define NANOVG_SW_IMPLEMENTATION
#include "nanovg_sw.h"
#include "nanovg_sw_utils.h"

#include "demo.h"
#include "perf.h"
#include "tests.c"

void errorcb(int error, const char* desc)
{
  printf("GLFW error %d: %s\n", error, desc);
}

int blowup = 0;
int screenshot = 0;
int premult = 0;

static void key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  NVG_NOTUSED(scancode);
  NVG_NOTUSED(mods);
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GL_TRUE);
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    blowup = !blowup;
  if (key == GLFW_KEY_S && action == GLFW_PRESS)
    screenshot = 1;
  if (key == GLFW_KEY_P && action == GLFW_PRESS)
    premult = !premult;
}

int main()
{
  GLFWwindow* window;
  DemoData data;
  NVGcontext* vg = NULL;
  GPUtimer gpuTimer;
  PerfGraph fps, cpuGraph, gpuGraph;
  double prevt = 0, cpuTime = 0;
  NVGSWUblitter* blitter = NULL;
  static void* blitterFB = NULL;
  int swRender = 0;

  if (!glfwInit()) {
    printf("Failed to init GLFW.");
    return -1;
  }

  initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");
  initGraph(&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
  initGraph(&gpuGraph, GRAPH_RENDER_MS, "GPU Time");

  glfwSetErrorCallback(errorcb);
//#ifndef _WIN32 // don't require this on win32, and works with more cards
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//#endif
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

#ifdef DEMO_MSAA
  glfwWindowHint(GLFW_SAMPLES, 4);
#endif
  window = glfwCreateWindow(1000, 600, "NanoVG", NULL, NULL);
//	window = glfwCreateWindow(1000, 600, "NanoVG", glfwGetPrimaryMonitor(), NULL);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  glfwSetKeyCallback(window, key);

  glfwMakeContextCurrent(window);

#ifdef __glad_h_
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
#elif defined(GLEW_VERSION)
    glewInit();
#endif

  if(swRender) {
   blitter = nvgswuCreateBlitter();
   vg = nvgswCreate(NVG_SRGB);
   // have to set pixel format before loading any images
   nvgswSetFramebuffer(vg, NULL, 800, 800, 0, 8, 16, 24);
  }
  else
    vg = nvglCreate(NVG_SRGB);  //NVG_DEBUG);

  if (!vg) {
    printf("Could not init nanovg.\n");
    return -1;
  }

  if (loadDemoData(vg, &data, NVG_IMAGE_SRGB) == -1)
    return -1;

  int useFramebuffer = !swRender;
  NVGLUframebuffer* nvgFB = NULL;
  if(useFramebuffer)
    nvgFB = nvgluCreateFramebuffer(vg, 0, 0, NVGLU_NO_NVG_IMAGE);  //glGenFramebuffers(1, &fbUser);

  glfwSwapInterval(0);

  initGPUTimer(&gpuTimer);

  glfwSetTime(0);
  prevt = glfwGetTime();

  while (!glfwWindowShouldClose(window))
  {
    double mx, my, t, dt;
    int winWidth, winHeight;
    int fbWidth, fbHeight;
    float pxRatio;
    int prevFBO;
    float gpuTimes[3];
    int i, n;

    t = glfwGetTime();
    dt = t - prevt;
    prevt = t;

    startGPUTimer(&gpuTimer);

    glfwGetCursorPos(window, &mx, &my);
    glfwGetWindowSize(window, &winWidth, &winHeight);
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    // Calculate pixel ration for hi-dpi devices.
    pxRatio = (float)fbWidth / (float)winWidth;

    if(swRender) {
      if(!blitterFB || fbWidth != blitter->width || fbHeight != blitter->height) {
        free(blitterFB);
        blitterFB = malloc(fbWidth*fbHeight*4);
      }
      memset(blitterFB, 0x3F, fbWidth*fbHeight*4);
      nvgswSetFramebuffer(vg, blitterFB, fbWidth, fbHeight, 0, 8, 16, 24);
    } else if(useFramebuffer) {
      prevFBO = nvgluBindFramebuffer(nvgFB);
      nvgluSetFramebufferSize(nvgFB, fbWidth, fbHeight, 0);
    }

    // Update and render
    nvgluSetViewport(0, 0, fbWidth, fbHeight);
    if (premult)
      nvgluClear(nvgRGBAf(0, 0, 0, 0));
    else
      nvgluClear(nvgRGBAf(0.3f, 0.3f, 0.32f, 1.0f));

    nvgBeginFrame(vg, winWidth, winHeight, pxRatio);

    renderDemo(vg, mx,my, winWidth,winHeight, t, blowup, &data);

    renderGraph(vg, 5,5, &fps);
    renderGraph(vg, 5+200+5,5, &cpuGraph);
    if (gpuTimer.supported)
      renderGraph(vg, 5+200+5+200+5,5, &gpuGraph);

    nvgEndFrame(vg);

    // Measure the CPU time taken excluding swap buffers (as the swap may wait for GPU)
    cpuTime = glfwGetTime() - t;

    updateGraph(&fps, dt);
    updateGraph(&cpuGraph, cpuTime);

    // We may get multiple results.
    n = stopGPUTimer(&gpuTimer, gpuTimes, 3);
    for (i = 0; i < n; i++)
      updateGraph(&gpuGraph, gpuTimes[i]);

    if(swRender)
      nvgswuBlit(blitter, blitterFB, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight);
    else if(useFramebuffer)
      nvgluBlitFramebuffer(nvgFB, prevFBO);  // blit to prev FBO and rebind it

    if (screenshot) {
      screenshot = 0;
      saveScreenShot(fbWidth, fbHeight, premult, "dump.png");
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  freeDemoData(vg, &data);

  if(useFramebuffer)
    nvgluDeleteFramebuffer(nvgFB);

  nvglDelete(vg);
  if(blitter) {
    free(blitterFB);
    nvgswuDeleteBlitter(blitter);
  }

  printf("Average Frame Time: %.2f ms\n", getGraphAverage(&fps) * 1000.0f);
  printf("          CPU Time: %.2f ms\n", getGraphAverage(&cpuGraph) * 1000.0f);
  printf("          GPU Time: %.2f ms\n", getGraphAverage(&gpuGraph) * 1000.0f);

  glfwTerminate();
  return 0;
}
