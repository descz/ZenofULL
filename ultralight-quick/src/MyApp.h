#pragma once
#include <AppCore/AppCore.h>
#include <JavaScriptCore/JavaScript.h>

using namespace ultralight;

class MyApp : public AppListener,
              public WindowListener,
              public LoadListener,
              public ViewListener {
public:
  MyApp();
  virtual ~MyApp();
  virtual void Run();
  virtual void OnUpdate() override;
  virtual void OnClose(Window* window) override;
  virtual void OnResize(Window* window, uint32_t width, uint32_t height) override;
  virtual void OnFinishLoading(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override;
  virtual void OnDOMReady(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override;
  virtual void OnChangeCursor(View* caller, Cursor cursor) override;
  virtual void OnChangeTitle(View* caller, const String& title) override;
  virtual void OnChangeURL(View* caller, const String& url) override;
  virtual void OnUpdateHistory(View* caller) override;

  JSValue OpenBrowser(const JSObject& this_object, const JSArgs& args);
  JSValue NavigateBrowser(const JSObject& this_object, const JSArgs& args);
  JSValue BrowserBack(const JSObject& this_object, const JSArgs& args);
  JSValue BrowserForward(const JSObject& this_object, const JSArgs& args);
  JSValue BrowserReload(const JSObject& this_object, const JSArgs& args);
  JSValue SetBrowserBounds(const JSObject& this_object, const JSArgs& args);
  JSValue CloseBrowser(const JSObject& this_object, const JSArgs& args);

protected:
  void EnsureBrowser();
  void NotifyBrowserState();
  void LayoutBrowser();

  RefPtr<App> app_;
  RefPtr<Window> window_;
  RefPtr<Overlay> overlay_;
  RefPtr<Overlay> browser_overlay_;
  int browser_x_ = 0;
  int browser_y_ = 0;
  uint32_t browser_width_ = 1;
  uint32_t browser_height_ = 1;
};
