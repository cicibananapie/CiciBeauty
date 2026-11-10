// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <mutex>
#include <optional>
#include <vector>

#include <QObject>

#include "gcd.h"
#include "obsutility.h"

struct IObsEventCallback;
typedef struct calldata calldata_t;
typedef struct signal_handler signal_handler_t;

// Per-input OBS state. Owns source/scene subscriptions; UI state and observer
// notifications stay on the UI thread. unbindSource also supports OBS teardown threads.
class ObsInfo : public QObject {
public:
    enum RotationType {
        Rotate_0,
        Rotate_90,
        Rotate_180,
        Rotate_270
    };

    enum HVMode {
        Unknown,
        HorizonalMode,
        VerticalMode
    };

    struct CanvasTransform {
        RotationType rotation_ = Rotate_0;
        bool flipHorizontal_ = false;
        bool flipVertical_ = false;
    };
    
public:
  ~ObsInfo() override;
  void bindSource(obs_source_t* source);
  void unbindSource();
  void refreshSceneBinding();
  void disconnectSceneBinding();

  void registerObserver(IObsEventCallback* cb);
  void unregisterObserver(IObsEventCallback* cb);
    
public:
    void setFrameSize(uint32_t w, uint32_t h);
    uint32_t frameWidth() const;
    uint32_t frameHeight() const;
    HVMode hvMode() const;
    const CanvasTransform& canvasTransform() const;
    const ObsUtility::VideoInputInfo& videoInputInfo() const;
    std::u8string videoInputDisplayName() const;
    
private:
  void unbindSourceLocked();
  std::optional<CanvasTransform> setupTransformListener(obs_source_t* source);
  void disconnectTransformListener();
  void setupVideoInputListener(obs_source_t* source);
  void disconnectVideoInputListener();
  static void onTransform(void* data, calldata_t* cd);
  static void onVideoInputSourceUpdate(void* data, calldata_t* cd);
  void handleTransform(calldata_t* cd);
  void handleVideoInputSourceUpdate(calldata_t* cd);
  void setCanvasTransform(const CanvasTransform& transform);
  void setVideoInputInfo(const ObsUtility::VideoInputInfo& info);
  void onFrameSizeChanged(uint32_t w, uint32_t h);
  void onCanvasTransformChanged(const CanvasTransform& transform);
  void onVideoInputChanged(const ObsUtility::VideoInputInfo& info);
  void computeHVMode();
    
private:
  std::mutex bindingMutex_;
  std::u8string sourceUuid_;
  signal_handler_t* transformHandler_ = nullptr;
  signal_handler_t* videoInputHandler_ = nullptr;
  std::vector<IObsEventCallback*> callbacks_;
  uint32_t w_ = 0;
  uint32_t h_ = 0;
  HVMode mode_ = Unknown;
  CanvasTransform canvasTransform_;

  uint32_t widthInWorkerThread_ = 0;
  uint32_t heightInWorkerThread_ = 0;
  ObsUtility::VideoInputInfo videoInputInfo_;
};

struct IObsEventCallback {
    virtual ~IObsEventCallback() = default;
    virtual void onHVModeChanged(ObsInfo::HVMode mode) {}
    virtual void onCanvasTransformChanged(const ObsInfo::CanvasTransform& transform) {}
    virtual void onVideoInputChanged(const ObsUtility::VideoInputInfo& info) {}
};
