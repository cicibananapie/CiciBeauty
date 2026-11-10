// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "obsinfo.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QCoreApplication>
#include <obs.h>
#include <obs-frontend-api.h>

#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>

#include "cicibase.h"
#include "log.h"

namespace {

obs_scene_t* currentScene = nullptr;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void drainUI() {
    bool drained = false;
    asyncRunUI([&] {
        drained = true;
    });
    while (!drained) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);
        QCoreApplication::processEvents();
    }
}

struct Observer : IObsEventCallback {
    int transforms = 0;
    int inputs = 0;
    int modes = 0;
    void onCanvasTransformChanged(const ObsInfo::CanvasTransform&) override {
        require(isMainThread(), "transform notification was not on UI thread");
        ++transforms;
    }
    void onVideoInputChanged(const ObsUtility::VideoInputInfo&) override {
        require(isMainThread(), "input notification was not on UI thread");
        ++inputs;
    }
    void onHVModeChanged(ObsInfo::HVMode) override {
        require(isMainThread(), "mode notification was not on UI thread");
        ++modes;
    }
};

struct Fixture {
    obs_source_t* a = obs_source_create("obsinfo_test_input", "Input A", nullptr, nullptr);
    obs_source_t* b = obs_source_create("obsinfo_test_input", "Input B", nullptr, nullptr);
    obs_scene_t* first = obs_scene_create("First scene");
    obs_scene_t* second = obs_scene_create("Second scene");
    obs_sceneitem_t* firstA = obs_scene_add(first, a);
    obs_sceneitem_t* firstB = obs_scene_add(first, b);
    obs_sceneitem_t* secondA = obs_scene_add(second, a);

    Fixture() {
        require(a && b && first && second && firstA && firstB && secondA, "OBS fixture creation failed");
        currentScene = first;
    }
    ~Fixture() {
        currentScene = nullptr;
        obs_sceneitem_remove(firstA);
        obs_sceneitem_remove(firstB);
        obs_sceneitem_remove(secondA);
        obs_source_remove(obs_scene_get_source(first));
        obs_source_remove(obs_scene_get_source(second));
        obs_scene_release(first);
        obs_scene_release(second);
        obs_source_remove(a);
        obs_source_remove(b);
        obs_source_release(a);
        obs_source_release(b);
        obs_wait_for_destroy_queue();
    }
};

void updateInput(obs_source_t* source) {
    calldata_t data;
    calldata_init(&data);
    calldata_set_ptr(&data, "source", source);
    signal_handler_signal(obs_source_get_signal_handler(source), "update", &data);
    calldata_free(&data);
}

void testInitialStateAndIndependentInputs() {
    Fixture f;
    obs_sceneitem_set_rot(f.firstA, 90);
    ObsInfo a, b;
    Observer observer;
    a.registerObserver(&observer);
    a.bindSource(f.a);
    b.bindSource(f.b);
    a.setFrameSize(1920, 1080);
    b.setFrameSize(1920, 1080);
    require(a.videoInputDisplayName() == u8"Input A", "initial input metadata missing");
    require(a.canvasTransform().rotation_ == ObsInfo::Rotate_90, "initial rotation missing");
    require(a.hvMode() == ObsInfo::VerticalMode && b.hvMode() == ObsInfo::HorizonalMode,
            "input states leaked across instances");
    obs_sceneitem_set_rot(f.firstB, 180);
    require(observer.transforms == 1, "unrelated source changed transform");
    vec2 scale;
    vec2_set(&scale, -1, 1);
    obs_sceneitem_set_scale(f.firstA, &scale);
    require(a.canvasTransform().flipHorizontal_, "flip signal was not received");
    require(observer.transforms == 2, "transform notification missing");
    a.setFrameSize(720, 1280);
    require(a.hvMode() == ObsInfo::HorizonalMode, "frame resize did not recompute orientation");
    updateInput(f.a);
    require(observer.inputs == 1, "unchanged device metadata was not deduplicated");
    obs_source_set_name(f.a, "Renamed input");
    std::thread([&] {
        updateInput(f.a);
    }).join();
    require(observer.inputs == 1, "worker callback notified synchronously");
    drainUI();
    require(a.videoInputDisplayName() == u8"Renamed input" && observer.inputs == 2,
            "worker device update was not delivered");
    a.unregisterObserver(&observer);
}

void testSceneSwitchAndDisconnect() {
    Fixture f;
    ObsInfo info;
    info.bindSource(f.a);
    info.setFrameSize(1920, 1080);
    obs_sceneitem_set_rot(f.firstA, 90);
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_90, "scene transform update failed");
    obs_sceneitem_set_rot(f.secondA, 180);
    currentScene = f.second;
    info.refreshSceneBinding();
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_180, "new scene snapshot missing");
    obs_sceneitem_set_rot(f.firstA, 270);
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_180, "old scene still connected");
    obs_sceneitem_set_rot(f.secondA, 270);
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_270, "new scene not connected");
    info.disconnectSceneBinding();
    obs_sceneitem_set_rot(f.secondA, 0);
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_270, "scene disconnect failed");
    info.refreshSceneBinding();
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_0, "scene reconnect failed");
}

void testUnbindRebindAndDestruction() {
    Fixture f;
    ObsInfo info;
    info.bindSource(f.a);
    obs_source_set_name(f.a, "Renamed input");
    std::thread([&] {
        obs_sceneitem_set_rot(f.firstA, 90);
        updateInput(f.a);
        info.setFrameSize(640, 480);
    }).join();
    drainUI();
    require(info.videoInputDisplayName() == u8"Renamed input" &&
                info.canvasTransform().rotation_ == ObsInfo::Rotate_90 && info.frameWidth() == 640 &&
                info.frameHeight() == 480,
            "worker updates were not delivered");
    std::thread([&] {
        info.unbindSource();
    }).join();
    info.bindSource(f.b);
    require(info.videoInputDisplayName() == u8"Input B", "new input metadata missing after rebind");
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_0, "new input transform missing after rebind");
    std::thread([&] {
        info.setFrameSize(800, 600);
    }).join();
    drainUI();
    require(info.frameWidth() == 800 && info.frameHeight() == 600, "frame size update after rebind failed");
    info.unbindSource();
    obs_source_set_name(f.b, "Detached input");
    updateInput(f.b);
    obs_sceneitem_set_rot(f.firstB, 90);
    require(info.videoInputDisplayName() == u8"Input B" && info.canvasTransform().rotation_ == ObsInfo::Rotate_0,
            "unbind left a listener active");

    auto disposable = std::make_unique<ObsInfo>();
    disposable->bindSource(f.a);
    std::thread([&] {
        obs_sceneitem_set_rot(f.firstA, 180);
        disposable->setFrameSize(800, 600);
        updateInput(f.a);
    }).join();
    disposable.reset();
    drainUI();
    obs_sceneitem_set_rot(f.firstA, 270);
    updateInput(f.a);
}

void testDeferredSceneBinding() {
    Fixture f;
    currentScene = nullptr;
    ObsInfo info;
    info.bindSource(f.a);
    require(info.videoInputDisplayName() == u8"Input A", "no scene prevented source binding");
    obs_sceneitem_set_rot(f.firstA, 90);
    currentScene = f.first;
    info.refreshSceneBinding();
    require(info.canvasTransform().rotation_ == ObsInfo::Rotate_90, "deferred scene binding failed");
}

} // namespace

// Supply only the frontend selection and device metadata. Subscriptions, scenes,
// source lifetimes, transforms and callback dispatch use real production code/libobs.
extern "C" obs_source_t* obs_frontend_get_current_scene() {
    return currentScene ? obs_source_get_ref(obs_scene_get_source(currentScene)) : nullptr;
}

ObsUtility::VideoInputInfo ObsUtility::videoInputInfo(obs_source_t* source) {
    VideoInputInfo info;
    info.displayName_ = to_u8(obs_source_get_name(source));
    info.obsSourceName_ = info.displayName_;
    info.sourceId_ = to_u8(obs_source_get_id(source));
    return info;
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    initCiciLogConfig(ciciDefaultLogLevel(), u8"obsinfo_tests.log");
    if (!initCiciBase() || !obs_startup("en-US", nullptr, nullptr)) {
        return 1;
    }
    const char* runtimeDir = std::getenv("CICI_OBS_RUNTIME_DIR");
    if (!runtimeDir) {
        return 1;
    }
    const std::filesystem::path runtime(runtimeDir);
    const auto dataPath = (runtime / "libobs.framework/Resources").string();
    const auto graphicsModule = (runtime / "libobs-opengl.dylib").string();
    obs_add_data_path(dataPath.c_str());
    obs_video_info video = {};
    video.graphics_module = graphicsModule.c_str();
    video.base_width = video.output_width = 320;
    video.base_height = video.output_height = 240;
    video.fps_num = 30;
    video.fps_den = 1;
    video.output_format = VIDEO_FORMAT_RGBA;
    video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_FULL;
    if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
        obs_shutdown();
        return 1;
    }
    obs_source_info source = {};
    source.id = "obsinfo_test_input";
    source.type = OBS_SOURCE_TYPE_INPUT;
    source.output_flags = OBS_SOURCE_VIDEO;
    source.get_name = [](void*) -> const char* {
        return "ObsInfo test input";
    };
    source.create = [](obs_data_t*, obs_source_t* s) -> void* {
        return s;
    };
    source.destroy = [](void*) {
    };
    source.get_width = [](void*) -> uint32_t {
        return 1920;
    };
    source.get_height = [](void*) -> uint32_t {
        return 1080;
    };
    obs_register_source(&source);
    int result = 0;
    try {
        testInitialStateAndIndependentInputs();
        testSceneSwitchAndDisconnect();
        testUnbindRebindAndDestruction();
        testDeferredSceneBinding();
        std::cout << "ObsInfo tests passed (4 scenarios)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    obs_shutdown();
    return result;
}
