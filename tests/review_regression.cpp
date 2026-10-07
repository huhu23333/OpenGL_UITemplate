#include "ui/UIApplication.h"
#include "ui/CanvasGroup.h"
#include "ui/Widgets.h"
#include "common/FrameRateCounter.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>

using namespace ui;
static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fill : Component {
    Color color = Color::Red();
    void OnDraw(Renderer2D& r) override { r.FillRect(0, 0, m_width, m_height, color); }
};
struct ClipEscape : Component {
    void OnDraw(Renderer2D& r) override {
        r.PushScissor(0, 0, 128, 96);
        r.FillRect(-100, -100, 300, 300, Color::Green());
        r.PopScissor();
    }
};
struct PersistentCapture : Component {
    RenderContext* context = nullptr;
    int cancellations = 0;
    bool OnMouseDown(MouseButton b, float, float) override {
        if (b != MouseButton::Left) return false;
        SetMouseCapture(true);
        context->SetMouseCaptured(true);
        return true;
    }
    void OnMouseCancel(MouseButton) override {
        ++cancellations;
        SetMouseCapture(false);
        context->SetMouseCaptured(false);
    }
};
struct Lines : Component {
    void OnDraw(Renderer2D& r) override {
        r.DrawLine(10, 10, 40, 10, 6, Color::Red());
        r.DrawLine(60, 10, 60, 40, 6, Color::Green());
        r.DrawLine(10, 50, 40, 80, 6, Color::Blue());
        r.DrawLine(90, 70, 90, 70, 8, Color::White());
        r.DrawLine(90, 80, 110, 80, 0, Color::White());
    }
};
static void Pixel(const cv::Mat& frame, int x, int y, cv::Vec4b expected, int tolerance = 2) {
    auto actual = frame.at<cv::Vec4b>(y, x);
    for (int c = 0; c < 4; ++c) {
        if (std::abs(int(actual[c]) - int(expected[c])) > tolerance) {
            std::cerr << "pixel " << x << ',' << y << " got " << actual << " expected " << expected << '\n';
            throw std::runtime_error("pixel mismatch");
        }
    }
}
static void Click(UIApplication& app, float x, float y) {
    app.InjectMousePos(x, y);
    app.InjectMouseButton(MouseButton::Left, true);
    app.RunFrame();
    app.InjectMouseButton(MouseButton::Left, false);
    app.RunFrame();
}
static void GeometryRegression() {
    Component root;
    root.SetRect(0, 0, 200, 100);
    auto* child = root.AddChild<Component>();
    child->SetRect(7, 9, 20, 30);
    float x, y, w, h;
    child->GetPosition(x, y); child->GetSize(w, h);
    Check(x == 7 && y == 9 && w == 20 && h == 30, "absolute setters query before layout");
    child->SetRectRatio(.1f, .2f, .5f, .5f);
    auto layout = [&]() { root.Layout({400, 200}, {}, {0, 0, 400, 200}, {}); };
    layout();
    Check(child->GetScreenRect().x == 20 && child->GetScreenRect().y == 20 &&
          child->GetScreenRect().w == 100 && child->GetScreenRect().h == 50, "SetRectRatio clears old offsets");
    child->SetPositionOffset(-5, 3); child->SetSizeOffset(-10, 7);
    layout();
    Check(child->GetScreenRect().x == 15 && child->GetScreenRect().h == 57, "ratio plus offset");
    root.SetSize(400, 200); layout();
    Check(child->GetScreenRect().x == 35 && child->GetScreenRect().w == 190, "parent resize recomputes ratio");
    child->SetRect(7, 9, 20, 30); layout();
    Check(child->GetScreenRect().x == 7 && child->GetScreenRect().w == 20, "absolute setters reset ratios");

    auto* canvas = root.AddChild<CanvasGroup>();
    canvas->SetRectRatio(.1f, .1f, .5f, .5f);
    auto* nested = canvas->AddChild<CanvasGroup>();
    nested->SetRectRatio(.25f, .25f, .5f, .5f);
    auto* leaf = nested->AddChild<Component>(); leaf->SetRectRatio(.5f, .5f, .5f, .5f);
    layout(); canvas->GetCanvasSize(w, h);
    Check(w == 200 && h == 100 && leaf->GetScreenRect().x == 140 && leaf->GetScreenRect().y == 70,
          "auto canvas and nested ratios available on first layout");
    canvas->SetCanvasSize(400, 200); canvas->SetScrollOffset(80, 40);
    layout();
    Check(nested->GetScreenRect().x == 60 && nested->GetScreenRect().y == 30 &&
          leaf->GetScreenRect().x == 160 && leaf->GetScreenRect().y == 80, "canvas dimensions and scroll coordinate spaces");
    Check(root.HitTest(170, 90) == leaf && root.HitTest(245, 90) != leaf, "nested canvas viewport hit clipping");
    auto* dropdown = root.AddChild<Dropdown>(); dropdown->SetItems({"A", "B"});
    dropdown->SetRectRatio(.1f, .1f, .5f, .2f); dropdown->SetOpen(true); layout();
    Check(dropdown->GetChild(0)->GetScreenRect().w == 200 &&
          dropdown->GetChild(1)->GetScreenRect().y == 60, "dropdown first ratio layout synchronizes children");
    dropdown->SetSizeRatio(.25f, .1f); layout();
    Check(dropdown->GetChild(0)->GetScreenRect().w == 100 &&
          dropdown->GetChild(1)->GetScreenRect().y == 40, "dropdown ratio resize has no one-frame lag");
}

static void SupersampleRegression(UIApplication& app) {
    const std::string label = "HgIi";
    const float scale = .6f;
    const int thickness = 2;
    int baseline = 0;
    cv::Size sz = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, scale, thickness, &baseline);
    int w = sz.width + 4, h = sz.height + baseline + 4;
    auto& cache = app.GetRenderer()->TextCache();
    cache.clear();
    for (float ss : {1.0f, 4.0f, 1.5f}) {
        auto* root = app.SetRoot<Component>();
        auto* text = root->AddChild<Text>(); text->SetRect(3, 5, w, h);
        text->SetText(label); text->SetFontScale(scale); text->SetFontThickness(thickness);
        text->SetTextSupersample(ss); app.RenderOffscreen();
        cv::Mat actual = app.GetFrameBGRA().clone();
        auto it = cache.find({label, scale, thickness, ss});
        Check(it != cache.end(), "supersample participates in cache key");
        int rw = (int)std::ceil(w * ss), rh = (int)std::ceil(h * ss);
        Check(it->second->Width() == rw && it->second->Height() == rh, "supersample scales logical bitmap padding and extent");
        // ss=1 is the exact pre-change raster; higher ss scales its baseline,
        // padding and stroke, rather than stretching independent font metrics.
        cv::Mat mask(rh, rw, CV_8UC1, cv::Scalar(0));
        cv::putText(mask, label, {cvRound(2 * ss), cvRound((2 + sz.height) * ss)},
                    cv::FONT_HERSHEY_SIMPLEX, scale * ss, cv::Scalar(255), cvRound(thickness * ss), cv::LINE_AA);
        cv::Mat white(rh, rw, CV_8UC1, cv::Scalar(255)), reference;
        cv::merge(std::vector<cv::Mat>{white, white, white, mask}, reference);
        root = app.SetRoot<Component>();
        auto* image = root->AddChild<ImageDisplay>(); image->SetRect(3, 5, w, h);
        image->SetBorderThickness(0); image->SetFitMode(ImageDisplay::FitMode::Stretch); image->SetImage(reference);
        app.RenderOffscreen();
        Check(cv::norm(actual, app.GetFrameBGRA(), cv::NORM_INF) == 0, "text pixels retain logical baseline and stroke; ss=1 exact regression");
    }
    Check(cache.size() == 3, "independent supersample cache entries");
    auto* root = app.SetRoot<Component>();
    auto* text = root->AddChild<Text>(); text->SetRect(0, 0, 128, 96); text->SetText(label);
    text->SetFontScale(scale); text->SetFontThickness(thickness);
    for (float ss : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        text->SetTextSupersample(ss); app.RenderOffscreen();
        Check(cache.size() == 3, "invalid supersample falls back to one without NaN cache keys");
    }
    for (float ss : {1e30f, 10000.0f}) {
        text->SetTextSupersample(ss); app.RenderOffscreen();
        Check(cache.size() == 3, "oversized raster rejected before allocation");
    }
    text->SetFontScale(20000); text->SetTextSupersample(.001f); app.RenderOffscreen();
    Check(cache.size() == 3 && glGetError() == GL_NO_ERROR, "logical and raster scales both validated");

    root = app.SetRoot<Component>();
    auto* box = root->AddChild<TextBox>(); box->SetRect(0, 0, 128, 40); box->SetText("IIII");
    box->SetFontScale(.6f); box->SetTextSupersample(4); box->SetCursorColor(Color::Red());
    int cursor = cv::getTextSize("II", cv::FONT_HERSHEY_SIMPLEX, .6f, 1, nullptr).width;
    Click(app, (float)(4 + cursor), 20);
    Pixel(app.GetFrameBGRA(), 4 + cursor, 20, {0, 0, 255, 255});
    app.InjectChar('X'); app.RunFrame();
    Check(box->GetText() == "IIXII", "supersample textbox hit and caret use logical advances");
    app.SetRoot<Component>();
}

static void AlphaRegression(UIApplication& app, UIApplication::Config cfg) {
    for (float alpha : {0.0f, .5f, 1.0f, 2.0f}) {
        cfg.clearColor = {0, 0, 0, alpha}; Check(app.Init(cfg), "alpha path Init");
        auto* root = app.GetRoot();
        auto* direct = root->AddChild<Fill>(); direct->SetRect(0, 0, 20, 20); direct->SetOpacity(.5f);
        auto* outer = root->AddChild<CanvasGroup>(); outer->SetRect(30, 0, 20, 20);
        auto* inner = outer->AddChild<CanvasGroup>(); inner->SetRectRatio(0, 0, 1, 1);
        auto* fill = inner->AddChild<Fill>(); fill->SetRectRatio(0, 0, 1, 1); fill->SetOpacity(.5f);
        app.RenderOffscreen();
        int a = alpha >= 1 ? 255 : (alpha == 0 ? 128 : 192);
        int r = alpha >= 1 ? 128 : (alpha == 0 ? 255 : 170);
        Pixel(app.GetFrameBGRA(), 5, 5, {0, 0, (uchar)r, (uchar)a});
        Pixel(app.GetFrameBGRA(), 35, 5, {0, 0, (uchar)r, (uchar)a});
        if (alpha >= 1) {
            cv::Mat channel; cv::extractChannel(app.GetFrameBGRA(), channel, 3);
            Check(cv::countNonZero(channel != 255) == 0, "opaque background alpha stays 255 across entire frame");
        }
    }
}

int main() {
    try {
        UIApplication app;
        UIApplication::Config cfg;
        cfg.showWindow = false;
        cfg.renderWidth = 128; cfg.renderHeight = 96;
        cfg.clearColor = Color::Transparent();
        Check(app.Init(cfg), "headless Init");
        GeometryRegression();
        SupersampleRegression(app);
        AlphaRegression(app, cfg);
        Check(app.Init(cfg), "restore transparent regression context");
        FrameRateCounter counter(3);
        Check(counter.fps() == 0 && counter.avg_frame_time() == 0, "empty FPS counter");
        for (int i = 0; i < 1000; ++i) counter.tick();
        Check(std::isfinite(counter.fps()) && counter.fps() > 0 && counter.avg_frame_time() > 0, "finite FPS samples");
        counter.set_window_size(1);
        Check(counter.window_size() == 1 && std::isfinite(counter.fps()), "FPS window shrink");
        counter.set_window_size(0); counter.tick();
        Check(counter.fps() == 0 && counter.avg_frame_time() == 0, "zero FPS window");
        counter.set_window_size(3); counter.reset(); counter.tick();
        Check(counter.fps() == 0, "FPS reset and first tick");

        app.SetRoot<Lines>(); app.RenderOffscreen();
        auto frame = app.GetFrameBGRA();
        Pixel(frame, 25, 7, {0, 0, 255, 255}); Pixel(frame, 25, 12, {0, 0, 255, 255});
        Pixel(frame, 25, 6, {0, 0, 0, 0}); Pixel(frame, 25, 13, {0, 0, 0, 0});
        Pixel(frame, 57, 25, {0, 255, 0, 255}); Pixel(frame, 63, 25, {0, 0, 0, 0});
        Pixel(frame, 25, 65, {255, 0, 0, 255}); Pixel(frame, 25, 70, {0, 0, 0, 0});
        Pixel(frame, 90, 70, {0, 0, 0, 0}); Pixel(frame, 100, 80, {0, 0, 0, 0});

        auto* root = app.SetRoot<Component>();
        auto addCapture = [&]() {
            auto* capture = app.GetRoot()->AddChild<PersistentCapture>();
            capture->SetRect(0, 0, 40, 40); capture->context = app.GetContext();
            Click(app, 10, 10);
            Check(capture->WantsMouseCapture() && app.GetContext()->IsMouseCaptured(), "capture persists after release");
            return capture;
        };
        auto* capture = addCapture();
        app.InjectMouseButton(MouseButton::Right, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Right, false); app.RunFrame();
        Check(capture->WantsMouseCapture(), "persistent capture survives unrelated release");
        capture->SetVisible(false); app.RunFrame();
        Check(!capture->WantsMouseCapture() && !app.GetContext()->IsMouseCaptured() && capture->cancellations == 1,
              "hidden persistent capture cancels after all buttons released");
        addCapture(); root->GetChildren().clear(); app.RunFrame();
        Check(!app.GetContext()->IsMouseCaptured(), "deleted persistent capture releases system cursor");
        addCapture(); root = app.SetRoot<Component>();
        Check(!app.GetContext()->IsMouseCaptured(), "root replacement releases system cursor");
        auto* direct = root->AddChild<Fill>(); direct->SetRect(2, 2, 12, 12); direct->SetOpacity(0.5f);
        auto* canvas = root->AddChild<CanvasGroup>(); canvas->SetRect(30, 10, 50, 50); canvas->SetOpacity(0);
        auto* nested = canvas->AddChild<CanvasGroup>(); nested->SetRect(5, 5, 30, 30);
        auto* nestedFill = nested->AddChild<Fill>(); nestedFill->SetRect(0, 0, 12, 12); nestedFill->SetOpacity(0.5f);
        auto* sibling = root->AddChild<Fill>(); sibling->SetRect(100, 4, 12, 12); sibling->color = Color::Blue();
        auto* clipped = root->AddChild<ClipEscape>(); clipped->SetRect(90, 70, 10, 10);
        app.RenderOffscreen();
        Pixel(app.GetFrameBGRA(), 4, 4, {0, 0, 255, 128});
        Pixel(app.GetFrameBGRA(), 37, 17, {0, 0, 255, 128});
        Pixel(app.GetFrameBGRA(), 104, 8, {255, 0, 0, 255});
        Pixel(app.GetFrameBGRA(), 89, 75, {0, 0, 0, 0});
        Pixel(app.GetFrameBGRA(), 95, 75, {0, 255, 0, 255});
        canvas->SetCanvasSize(80, 80); nested->SetCanvasSize(40, 40);
        app.RenderOffscreen(); Pixel(app.GetFrameBGRA(), 37, 17, {0, 0, 255, 128});
        Check(glGetError() == GL_NO_ERROR, "GL errors in nested rendering");

        // cv::Mat upload, FBO compositing and readback all preserve top/bottom.
        root = app.SetRoot<Component>();
        cv::Mat source(4, 4, CV_8UC4, cv::Scalar(255, 0, 0, 255));
        source(cv::Rect(0, 0, 4, 2)).setTo(cv::Scalar(0, 0, 255, 255));
        auto* image = root->AddChild<ImageDisplay>(); image->SetRect(0, 0, 40, 40); image->SetImage(source);
        canvas = root->AddChild<CanvasGroup>(); canvas->SetRect(50, 0, 40, 40);
        auto* image2 = canvas->AddChild<ImageDisplay>(); image2->SetRect(0, 0, 40, 40); image2->SetImage(source);
        app.RenderOffscreen();
        Pixel(app.GetFrameBGRA(), 20, 5, {0, 0, 255, 255}); Pixel(app.GetFrameBGRA(), 20, 35, {255, 0, 0, 255});
        Pixel(app.GetFrameBGRA(), 70, 5, {0, 0, 255, 255}); Pixel(app.GetFrameBGRA(), 70, 35, {255, 0, 0, 255});
        source.setTo(cv::Scalar(0, 0, 255, 128)); image->SetImage(source); image2->SetImage(source);
        app.RenderOffscreen();
        Pixel(app.GetFrameBGRA(), 20, 20, {0, 0, 255, 128}); Pixel(app.GetFrameBGRA(), 70, 20, {0, 0, 255, 128});
        image2->SetBorder(2, Color::Green()); app.RenderOffscreen();
        Pixel(app.GetFrameBGRA(), 50, 10, {0, 255, 0, 255});
        int imageClicks = 0;
        image->SetRect(0, 0, 40, 60); image->SetOnClick([&](ImageDisplay*, float, float) { ++imageClicks; });
        Click(app, 20, 3); Check(imageClicks == 0, "contain padding click");
        Click(app, 20, 20); Check(imageClicks == 1, "image click");
        int completedBoxes = 0;
        image->SetOnBoxDrawn([&](ImageDisplay*, float, float, float, float) { ++completedBoxes; });
        app.InjectMousePos(15, 20); app.InjectMouseButton(MouseButton::Left, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Right, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Right, false); app.RunFrame();
        Check(image->WantsMouseCapture(), "image capture survives unrelated button release");
        app.InjectMousePos(35, 40); app.InjectMouseButton(MouseButton::Left, false); app.RunFrame();
        Check(completedBoxes == 1 && !image->WantsMouseCapture(), "image drag releases capture and commits once");
        app.InjectMousePos(20, 20); app.InjectMouseButton(MouseButton::Left, true); app.RunFrame();
        int boxes = 0; image->SetOnBoxDrawn([&](ImageDisplay*, float, float, float, float) { ++boxes; });
        image->SetVisible(false); app.RunFrame(); app.InjectMouseButton(MouseButton::Left, false); app.RunFrame();
        Check(boxes == 0 && imageClicks == 1 && !image->WantsMouseCapture(), "hidden image cancels without callback");
        image->SetImage(cv::Mat(2, 2, CV_32FC3)); Check(!image->HasImage(), "unsupported image rejected");

        // FBO, viewport, scissor and pack state are restored, independent read/draw bindings.
        GLuint fb[2]; glGenFramebuffers(2, fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[0]); glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
        glViewport(3, 4, 17, 19); glEnable(GL_SCISSOR_TEST); glScissor(1, 2, 3, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 200); glPixelStorei(GL_PACK_SKIP_ROWS, 1);
        app.RenderOffscreen();
        GLint value, vp[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value); Check(GLuint(value) == fb[0], "draw binding");
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value); Check(GLuint(value) == fb[1], "read binding");
        glGetIntegerv(GL_VIEWPORT, vp); Check(vp[0] == 3 && vp[3] == 19, "viewport restore");
        glGetIntegerv(GL_PACK_ROW_LENGTH, &value); Check(value == 200, "pack restore");
        Check(glIsEnabled(GL_SCISSOR_TEST), "scissor restore");
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(2, fb);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0); glDisable(GL_SCISSOR_TEST);
        Check(glGetError() == GL_NO_ERROR, "GL errors in readback");

        root = app.SetRoot<Component>();
        auto* scroll = root->AddChild<ScrollView>(); scroll->SetRect(10, 10, 50, 50); scroll->SetCanvasSize(50, 160);
        auto* item = scroll->AddChild<Button>(); item->SetRect(0, 0, 50, 160);
        int clicks = 0; item->SetOnClick([&](Button*) { ++clicks; });
        app.InjectMousePos(20, 20); app.InjectScroll(-1); app.RunFrame();
        Check(scroll->GetScrollY() == 40, "wheel over button bubbles to scrollview");
        Check(item->GetScreenRect().y == -30, "scrolled hit coordinates");
        Check(root->HitTest(20, 5) == root, "parent clips hit test");
        Click(app, 20, 20); Check(clicks == 1, "scrolled click");
        Click(app, 57, 55); Check(clicks == 1 && scroll->GetScrollY() > 40, "scrollbar wins hit test");
        scroll->SetScrollY(-40); Check(scroll->GetScrollY() == 0, "negative scroll clamp");
        scroll->SetScrollY(100); scroll->SetSize(50, 160); app.RunFrame(); Check(scroll->GetScrollY() == 0, "resize reclamp");

        root = app.SetRoot<Component>();
        auto* button = root->AddChild<Button>(); button->SetRect(0, 0, 30, 20);
        clicks = 0; button->SetOnClick([&](Button*) { ++clicks; });
        app.InjectMousePos(10, 10); app.InjectMouseButton(MouseButton::Left, true); app.RunFrame();
        app.InjectMousePos(90, 90); app.InjectMouseButton(MouseButton::Left, false); app.RunFrame();
        Check(!button->IsPressed() && clicks == 0, "button release outside");
        app.InjectMousePos(10, 10); app.InjectMouseButton(MouseButton::Left, true);
        app.InjectMouseButton(MouseButton::Left, false); app.RunFrame(); Check(clicks == 1, "same frame click");
        auto* slider = root->AddChild<Slider>(); slider->SetRect(0, 30, 60, 20);
        slider->SetRange(1000000, 1000000); Check(slider->GetNormalized() == 0, "zero range"); slider->SetRange(0, 1);
        app.InjectMousePos(10, 35); app.InjectMouseButton(MouseButton::Left, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Right, true); app.RunFrame(); app.InjectMouseButton(MouseButton::Right, false); app.RunFrame();
        app.InjectMousePos(100, 90); app.RunFrame(); Check(slider->IsDragging() && slider->GetValue() == 1, "unrelated release preserves capture");
        app.InjectMousePos(30, 40); app.InjectMouseButton(MouseButton::Left, false); app.RunFrame();
        Check(!slider->IsDragging() && std::fabs(slider->GetValue() - 0.5f) < 0.001f, "release position updates slider");
        app.InjectMousePos(10, 35); app.InjectMouseButton(MouseButton::Left, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Right, true); app.RunFrame();
        app.InjectMouseButton(MouseButton::Left, false); app.RunFrame();
        Check(!slider->IsDragging() && !slider->WantsMouseCapture(), "primary release stops drag while another button held");
        app.InjectMouseButton(MouseButton::Right, false); app.RunFrame();
        app.InjectMousePos(-2, -2); app.RunFrame(); Check(!app.GetInput().mouseInside, "headless mouse bounds");

        auto* box = root->AddChild<TextBox>(); box->SetRect(0, 55, 120, 30); box->SetText("A中B");
        Click(app, 115, 65); app.InjectKey(KEY_BACKSPACE, true); app.RunFrame(); app.InjectKey(KEY_BACKSPACE, false); app.RunFrame();
        Check(box->GetText() == "A中", "UTF8 click end/backspace");
        app.InjectChar(0x1F600); app.InjectChar(0xD800); app.InjectChar(0x110000); app.RunFrame();
        Check(box->GetText() == "A中😀", "valid Unicode only");
        app.SetFocus(nullptr); Check(!box->IsFocused(), "SetFocus notification"); app.SetFocus(box);
        box->SetVisible(false); app.InjectChar('x'); app.RunFrame(); Check(!box->IsFocused() && !app.GetFocus(), "hidden focus canceled");

        // Callback may replace the root and destroy its own callable and dispatch target.
        button->SetOnClick([&](Button*) { app.SetRoot<Component>(); });
        Click(app, 10, 10); Check(app.GetRoot()->GetChildCount() == 0, "callback root replacement");
        root = app.GetRoot();
        auto* dropdown = root->AddChild<Dropdown>(); dropdown->SetItems({"A", "B"}); dropdown->SetRect(5, 5, 70, 20);
        dropdown->SetItemHeight(25); dropdown->SetOpen(true); app.RenderOffscreen();
        Check(root->HitTest(10, 55) != root, "dropdown layout after items before size");
        Click(app, 10, 55); Check(dropdown->GetSelectedIndex() == 1 && !dropdown->IsOpen(), "dropdown choose second item");
        dropdown->SetOpen(true); app.RunFrame(); dropdown->SetItems({"C"}); app.RunFrame();
        Check(glGetError() == GL_NO_ERROR, "GL errors at end");

        app.SetRoot(std::unique_ptr<Component>{}); app.RenderOffscreen();
        Pixel(app.GetFrameBGRA(), 0, 0, {0, 0, 0, 0});
        Check(!app.GetInput().IsMouseDown(MouseButton::Count), "mouse sentinel is safe");
        app.GetRenderer()->DrawTexture(nullptr, 0, 0, 10, 10);
        for (int i = 0; i < 2; ++i) {
            Check(app.Init(cfg), "reinitialize app");
            auto* text = app.GetRoot()->AddChild<Text>(); text->SetRect(0, 0, 100, 30); text->SetText("cache lifecycle");
            app.RenderOffscreen(); Check(glGetError() == GL_NO_ERROR, "text cache context lifecycle");
        }
        {
            UIApplication other; Check(other.Init(cfg), "second simultaneous context"); other.RenderOffscreen();
        }
        app.RenderOffscreen(); Check(!app.GetFrame().empty() && glGetError() == GL_NO_ERROR, "surviving EGL context");
        auto* shutdownBox = app.GetRoot()->AddChild<TextBox>(); shutdownBox->SetRect(0, 40, 100, 20);
        shutdownBox->SetOnSubmit([&](TextBox*) { app.Shutdown(); }); app.SetFocus(shutdownBox);
        app.InjectKey(KEY_ENTER, true); app.RunFrame(); Check(!app.GetContext(), "shutdown from callback");
        app.Shutdown(); Check(Component::GlobalInput() == nullptr, "global input teardown");
        cfg.renderWidth = 0; Check(!app.Init(cfg), "zero resolution rejected");
        std::cout << "PASS: rendering, alpha, clipping, texture orientation, FBO state, input, UTF-8, lifecycle\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
