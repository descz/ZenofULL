#include "MyApp.h"
#include <algorithm>

#define WINDOW_WIDTH 1440
#define WINDOW_HEIGHT 900

MyApp::MyApp() {
  Settings settings;
  Config config;
  config.scroll_timer_delay = 1.0 / 90.0;
  app_ = App::Create(settings, config);
  window_ = Window::Create(app_->main_monitor(), WINDOW_WIDTH, WINDOW_HEIGHT, false,
    kWindowFlags_Titled | kWindowFlags_Resizable | kWindowFlags_Maximizable);
  window_->SetTitle("Zeno Agent");

  overlay_ = Overlay::Create(window_, 1, 1, 0, 0);
  OnResize(window_.get(), window_->width(), window_->height());
  overlay_->view()->set_load_listener(this);
  overlay_->view()->set_view_listener(this);
  overlay_->view()->LoadURL("file:///index.html");

  app_->set_listener(this);
  window_->set_listener(this);
}

MyApp::~MyApp() {
  if (browser_overlay_) {
    browser_overlay_->view()->set_load_listener(nullptr);
    browser_overlay_->view()->set_view_listener(nullptr);
  }
}

void MyApp::Run() { app_->Run(); }
void MyApp::OnUpdate() {}
void MyApp::OnClose(Window*) { app_->Quit(); }

void MyApp::OnResize(Window*, uint32_t width, uint32_t height) {
  overlay_->Resize(width, height);
  LayoutBrowser();
}

void MyApp::LayoutBrowser() {
  if (!browser_overlay_) return;
  const uint32_t max_w = window_->width();
  const uint32_t max_h = window_->height();
  const uint32_t x = std::min<uint32_t>(std::max(0, browser_x_), max_w > 1 ? max_w - 1 : 0);
  const uint32_t y = std::min<uint32_t>(std::max(0, browser_y_), max_h > 1 ? max_h - 1 : 0);
  const uint32_t w = std::max<uint32_t>(1, std::min(browser_width_, max_w - x));
  const uint32_t h = std::max<uint32_t>(1, std::min(browser_height_, max_h - y));
  browser_overlay_->MoveTo(x, y);
  browser_overlay_->Resize(w, h);
}

void MyApp::EnsureBrowser() {
  if (browser_overlay_) return;
  browser_overlay_ = Overlay::Create(window_, browser_width_, browser_height_, browser_x_, browser_y_);
  browser_overlay_->view()->set_load_listener(this);
  browser_overlay_->view()->set_view_listener(this);
  browser_overlay_->Show();
  browser_overlay_->Focus();
}

void MyApp::OnFinishLoading(View* caller, uint64_t, bool is_main_frame, const String&) {
  if (caller == (browser_overlay_ ? browser_overlay_->view().get() : nullptr) && is_main_frame)
    NotifyBrowserState();
}

void MyApp::OnDOMReady(View* caller, uint64_t, bool is_main_frame, const String&) {
  if (caller != overlay_->view().get() || !is_main_frame) return;
  RefPtr<JSContext> context = caller->LockJSContext();
  SetJSContext(context->ctx());
  JSObject global = JSGlobalObject();
  global["ZenoOpenBrowser"] = BindJSCallbackWithRetval(&MyApp::OpenBrowser);
  global["ZenoNavigateBrowser"] = BindJSCallbackWithRetval(&MyApp::NavigateBrowser);
  global["ZenoBrowserBack"] = BindJSCallbackWithRetval(&MyApp::BrowserBack);
  global["ZenoBrowserForward"] = BindJSCallbackWithRetval(&MyApp::BrowserForward);
  global["ZenoBrowserReload"] = BindJSCallbackWithRetval(&MyApp::BrowserReload);
  global["ZenoSetBrowserBounds"] = BindJSCallbackWithRetval(&MyApp::SetBrowserBounds);
  global["ZenoCloseBrowser"] = BindJSCallbackWithRetval(&MyApp::CloseBrowser);
}

JSValue MyApp::OpenBrowser(const JSObject&, const JSArgs& args) {
  EnsureBrowser();
  browser_overlay_->Show();
  browser_overlay_->Focus();
  if (args.size() > 0) browser_overlay_->view()->LoadURL(args[0].ToString());
  return JSValue(true);
}

JSValue MyApp::NavigateBrowser(const JSObject&, const JSArgs& args) {
  EnsureBrowser();
  if (args.size() > 0) browser_overlay_->view()->LoadURL(args[0].ToString());
  return JSValue(true);
}

JSValue MyApp::BrowserBack(const JSObject&, const JSArgs&) {
  if (browser_overlay_ && browser_overlay_->view()->CanGoBack()) browser_overlay_->view()->GoBack();
  return JSValue(true);
}

JSValue MyApp::BrowserForward(const JSObject&, const JSArgs&) {
  if (browser_overlay_ && browser_overlay_->view()->CanGoForward()) browser_overlay_->view()->GoForward();
  return JSValue(true);
}

JSValue MyApp::BrowserReload(const JSObject&, const JSArgs&) {
  if (browser_overlay_) browser_overlay_->view()->Reload();
  return JSValue(true);
}

JSValue MyApp::SetBrowserBounds(const JSObject&, const JSArgs& args) {
  if (args.size() < 4) return JSValue(false);
  browser_x_ = args[0].ToInteger();
  browser_y_ = args[1].ToInteger();
  browser_width_ = std::max(1, args[2].ToInteger());
  browser_height_ = std::max(1, args[3].ToInteger());
  LayoutBrowser();
  return JSValue(true);
}

JSValue MyApp::CloseBrowser(const JSObject&, const JSArgs&) {
  if (browser_overlay_) {
    browser_overlay_->Hide();
    browser_overlay_->Unfocus();
  }
  return JSValue(true);
}

void MyApp::NotifyBrowserState() {
  if (!browser_overlay_) return;
  RefPtr<JSContext> context = overlay_->view()->LockJSContext();
  SetJSContext(context->ctx());
  JSObject global = JSGlobalObject();
  JSValue callback = global["onZenoBrowserState"];
  if (!callback.IsFunction()) return;
  JSFunction fn = callback.ToFunction();
  fn(JSArgs({ JSValue(browser_overlay_->view()->url()),
              JSValue(browser_overlay_->view()->CanGoBack()),
              JSValue(browser_overlay_->view()->CanGoForward()) }));
}

void MyApp::OnChangeURL(View* caller, const String&) {
  if (browser_overlay_ && caller == browser_overlay_->view().get()) NotifyBrowserState();
}

void MyApp::OnUpdateHistory(View* caller) {
  if (browser_overlay_ && caller == browser_overlay_->view().get()) NotifyBrowserState();
}

void MyApp::OnChangeCursor(View*, Cursor cursor) { window_->SetCursor(cursor); }
void MyApp::OnChangeTitle(View* caller, const String& title) {
  if (caller == overlay_->view().get()) window_->SetTitle(title.utf8().data());
}
