// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "binarymsg.h"

/**
    class UpdateServer is responsible for asking update server, downloading, and installing new version.
    UpdateServer lives in another process.
    UpdateClient communicate with UpdateServer by IPC.
 */

enum class UpdateType: uint32_t {
    NoNeedUpdate,
    SilentUpdate,
    PromptedForegroundUpdate,
    PromptedBackgroundUpdate,
    ForceUpdate
};

enum class UpdateStage: uint32_t {
    InvalidStage,
    DownloadPackage,
    InstallPackage
};

enum class UpdateFinishResult: uint32_t {
    Succeeded,
    Failed,
    Cancelled
};

//calls from cicibeauty to ciciupdater
const uint32_t kMsgPleaseQuit = 1000;
const uint32_t kMsgStartUpdate = 1001;
const uint32_t kMsgIgnoreUpdate = 1002;
const uint32_t kMsgCancelUpdate = 1003;

struct PleaseQuitMsg: public IPCMessage {
    bool restartOBS_ = false;
    std::u8string path_;
    uint32_t obsPid_ = 0;
    
    void setPath(const std::u8string& path) { path_ = path; }
    std::u8string path() const { return path_; }
    
    uint32_t id() const override {return kMsgPleaseQuit;}
    
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t sz = bin.push(restartOBS_);
        sz += bin.push(path_);
        sz += bin.push(obsPid_);
        
        return sz;
    }
    
    bool unpack(BinaryUnpack& bin) override {
	    if (!bin.pop(restartOBS_) || !bin.pop(path_) || !bin.pop(obsPid_)) {
            return false;
        }

        return true;
    }
};

struct StartUpdateMsg: public IPCMessage {
    UpdateType type_ = UpdateType::NoNeedUpdate;
    
    uint32_t id() const override {return kMsgStartUpdate;}
    
    uint32_t pack(BinaryPack& bin) const override {
        return bin.push(static_cast<uint32_t>(type_));
    }
    
    bool unpack(BinaryUnpack& bin) override {
        uint32_t t = 0;
        if (!bin.pop(t)) {
            return false;
        }
        
        type_ = static_cast<UpdateType>(t);
        
        return true;
    }
};

struct IgnoreUpdateMsg: public StartUpdateMsg {
    uint32_t id() const override {return kMsgIgnoreUpdate;}
};

struct CancelUpdateMsg: public StartUpdateMsg {
    uint32_t id() const override {return kMsgCancelUpdate;}
};

//callbacks from ciciupdater to cicibeauty
const uint32_t kMsgUpdateInfo = 1100;
const uint32_t kMsgUpdateProgress = 1101;
const uint32_t kMsgUpdateFinished = 1102;

struct UpdateInfoMsg:public IPCMessage {
    struct Description {
        std::u8string text_;
        std::u8string image_;
        
        void setText(const std::u8string& text) { text_ = text; }
        void setImage(const std::u8string& image) { image_ = image; }
        std::u8string text() const { return text_; }
        std::u8string image() const { return image_; }
        
        uint32_t pack(BinaryPack& bin) const {
            uint32_t sz = bin.push(text_);
            sz += bin.push(image_);
            
            return sz;
        }
        
        bool unpack(BinaryUnpack& bin) {
	        if (!bin.pop(text_) || !bin.pop(image_)) {
                return false;
            }

            return true;
        }
    };
    std::u8string version_;
    UpdateType type_ = UpdateType::NoNeedUpdate;
    std::vector<Description> description_;
    
    void setVersion(const std::u8string& version) { version_ = version; }
    std::u8string version() const { return version_; }
    
    uint32_t id() const override {return kMsgUpdateInfo;}
    
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t sz = bin.push(version_);
        sz += bin.push(static_cast<uint32_t>(type_));
        sz += bin.push(description_);
        
        return sz;
    }
    
    bool unpack(BinaryUnpack& bin) override {
	    if (!bin.pop(version_)) {
            return false;
        }
        
        uint32_t t = 0;
        if (!bin.pop(t)) {
            return false;
        }
        
        type_ = static_cast<UpdateType>(t);
        
        return bin.pop(description_);
    }
};

struct UpdateProgressMsg:public IPCMessage {
    UpdateType type_ = UpdateType::NoNeedUpdate;
    UpdateStage stage_ = UpdateStage::InvalidStage;
    float progress_ = 0.0f; //0.0~1.0
    
    uint32_t id() const override {return kMsgUpdateProgress;}
    
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t sz = bin.push(static_cast<uint32_t>(type_));
        sz += bin.push(static_cast<uint32_t>(stage_));
        sz += bin.push(progress_);
        
        return sz;
    }
    
    bool unpack(BinaryUnpack& bin) override {
        uint32_t t = 0;
        if (!bin.pop(t)) {
            return false;
        }
        type_ = static_cast<UpdateType>(t);
        
        if (!bin.pop(t)) {
            return false;
        }
        stage_ = static_cast<UpdateStage>(t);
        
        return bin.pop(progress_);
    }
};

struct UpdateFinishedMsg:public IPCMessage {
    UpdateType type_ = UpdateType::NoNeedUpdate;
    UpdateFinishResult result_ = UpdateFinishResult::Failed;
    
    uint32_t id() const override {return kMsgUpdateFinished;}
    
    uint32_t pack(BinaryPack& bin) const override {
        uint32_t sz = bin.push(static_cast<uint32_t>(type_));
        sz += bin.push(static_cast<uint32_t>(result_));
        
        return sz;
    }
    
    bool unpack(BinaryUnpack& bin) override {
        uint32_t t = 0;
        if (!bin.pop(t)) {
            return false;
        }
        type_ = static_cast<UpdateType>(t);

        if (!bin.pop(t) || t > static_cast<uint32_t>(UpdateFinishResult::Cancelled)) {
            return false;
        }
        result_ = static_cast<UpdateFinishResult>(t);
        return true;
    }
};
