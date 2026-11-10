// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <map>

struct IPCMessage;
struct BinaryPack : public BinaryStream {
    uint32_t push(bool d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    uint32_t push(int8_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(int16_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(int32_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(int64_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(uint8_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(uint16_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(uint32_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(uint64_t d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    uint32_t push(float d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(double d) {
        const char* data = reinterpret_cast<const char*>(&d);
        this->append(data, sizeof(d));
        
        return sizeof(d);
    }
    
    uint32_t push(const BinaryStream& d) {
        uint32_t len = static_cast<uint32_t>(d.size());
        uint32_t sz = push(len);
        this->append(d);
        sz += len;
        
        return sz;
    }

    uint32_t push(const std::u8string &d) {
	    uint32_t len = static_cast<uint32_t>(d.size());
	    uint32_t sz = push(len);
	    this->append(to_char(d));
	    sz += len;

	    return sz;
    }
    
    inline uint32_t push(const IPCMessage& msg);
    
    template<typename T>
    uint32_t push(const T& d) {
        return d.pack(*this);
    }
    
    template<typename T>
    uint32_t push(const std::vector<T>& v) {
        uint32_t len = static_cast<uint32_t>(v.size());
        uint32_t sz = push(len);
        for (auto i = 0; i < len; i++) {
            sz += push(v[i]);
        }
        
        return sz;
    }
    
    template<typename KeyT, typename ValueT>
    uint32_t push(const std::map<KeyT, ValueT>& m) {
        uint32_t len = static_cast<uint32_t>(m.size());
        uint32_t sz = push(len);
        for (auto it = m.begin(); it != m.end(); it++) {
            sz += push((*it).first);
            sz += push((*it).second);
        }
        
        return sz;
    }
    
    friend struct IPCMessage;
    
private:
    void fixLengthElement(uint32_t len) {
        uint32_t* szPos = reinterpret_cast<uint32_t*>(data());
        *szPos = len;
    }
};

struct BinaryUnpack {
    static bool peekRawMsg(BinaryStream& buf, BinaryStream& msg) {
        if (buf.size() < sizeof(uint32_t)) {
            return false;
        }
        
        uint32_t sz = *reinterpret_cast<const uint32_t*>(buf.data());
        
        if (sz > buf.size()) {
            return false;
        }
        
        msg = buf.substr(0, sz);
        buf = buf.substr(sz);

        return true;
    }
    
    static uint32_t whitchMsg(const uint8_t* data, int32_t len) {
        const uint32_t* pos = reinterpret_cast<const uint32_t*>(data);
        uint32_t sz = *pos;
        if (sz > len) {
            return 0;
        }
        
        pos++;
        
        return *pos;
    }
    
    static uint32_t whitchMsg(const BinaryStream& msg) {
        return whitchMsg(
                reinterpret_cast<const uint8_t*>(msg.data()),
                static_cast<int32_t>(msg.size())
            );
    }
    
    BinaryUnpack(uint8_t* data, int32_t len):data_(data), len_(len) {
    }
    
    void skipHeader() {
        data_ += 2*sizeof(uint32_t);
        len_ -= 2*sizeof(uint32_t);
        
        assert(len_ >= 0);
    }
    
    bool pop(bool& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(int8_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(int16_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(int32_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(int64_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(uint8_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(uint16_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(uint32_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(uint64_t& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(float& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(double& d) {
        if (len_ < sizeof(d)) {
            return false;
        }
        
        d = *reinterpret_cast<decltype(&d)>(data_);
        data_ += sizeof(d);
        len_ -= sizeof(d);
        
        return true;
    }
    
    bool pop(BinaryStream& d) {
        if (len_ < sizeof(uint32_t)) {
            return false;
        }
        
        uint32_t sz = *reinterpret_cast<uint32_t*>(data_);
        if (len_ < sizeof(uint32_t) + sz) {
            return false;
        }
        
        data_ += sizeof(uint32_t);
        d.assign(reinterpret_cast<const char*>(data_), sz);
        data_ += sz;
        
        len_ -= sz + sizeof(uint32_t);
        assert(len_ >= 0);
        
        return true;
    }

    bool pop(std::u8string &d) { 
        BinaryStream tmp;
        auto b = pop(tmp);

        if (b) {
		    d = to_u8(tmp);
        }

        return b;
    }
    
    inline bool pop(IPCMessage& msg);
    
    template<typename T>
    bool pop(T& d) {
        return d.unpack(*this);
    }
    
    template<typename T>
    bool pop(std::vector<T>& v) {
        if (len_ < sizeof(uint32_t)) {
            return false;
        }
        
        uint32_t count = *reinterpret_cast<uint32_t*>(data_);
        data_ += sizeof(uint32_t);
        len_ -= sizeof(uint32_t);
        
        for (auto i = 0; i < count; i++) {
            T t;
            if (!pop(t)) {
                return false;
            }
            
            v.push_back(t);
        }
        
        return true;
    }
    
    template<typename KeyT, typename ValueT>
    bool pop(std::map<KeyT, ValueT>& m) {
        if (len_ < sizeof(uint32_t)) {
            return false;
        }
        
        uint32_t count = *reinterpret_cast<uint32_t*>(data_);
        data_ += sizeof(uint32_t);
        len_ -= sizeof(uint32_t);
        
        for (auto i = 0; i < count; i++) {
            KeyT k;
            ValueT v;
            if (!pop(k)) {
                return false;
            }
            
            if (!pop(v)) {
                return false;
            }
            
            m[k] = v;
        }
        
        return true;
    }
    
private:
    uint8_t* data_ = nullptr;
    int32_t len_ = 0;
};

struct IPCMessage {
    virtual ~IPCMessage() = default;
protected:
    virtual uint32_t id() const = 0;
    virtual uint32_t pack(BinaryPack& bin) const = 0;
    virtual bool unpack(BinaryUnpack& bin) = 0;
    
public:
    uint32_t toBinary(BinaryPack& bin) const {
        uint32_t sz = 0;
        sz += bin.push(sz);
        sz += bin.push(id());
        sz += pack(bin);
        
        bin.fixLengthElement(sz);
        return sz;
    }
    
    bool fromBinary(const BinaryStream& rawMsg) {
        uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(rawMsg.data()));
        int32_t len = static_cast<int32_t>(rawMsg.size());
        BinaryUnpack bin(data, len);
        
        return fromBinary(bin);
    }
private:
    friend struct BinaryUnpack;
    bool fromBinary(BinaryUnpack& bin) {
        bin.skipHeader();
        
        return unpack(bin);
    }
};

inline uint32_t BinaryPack::push(const IPCMessage& msg) {
    return msg.toBinary(*this);
}

inline bool BinaryUnpack::pop(IPCMessage& msg) {
    return msg.fromBinary(*this);
}

const uint32_t KRemoteConnected = 0;    //remote to me,param: remote pid
const uint32_t KRemoteConnectedAck = 1; //me to remote,param: remote pid
const uint32_t KRemoteClosed = 2;   //remote to me
const uint32_t kLogMsgId = 4;

struct RemoteConnectMsg: public IPCMessage {
    uint32_t pid_;
    
    RemoteConnectMsg(uint32_t pid = 0):pid_(pid) {}
    
protected:
    uint32_t id() const override {
        return KRemoteConnected;
    }
    
    uint32_t pack(BinaryPack& bin) const override{
        return bin.push(pid_);
    }
    
    bool unpack(BinaryUnpack& bin) override{
        return bin.pop(pid_);
    }
};

struct RemoteConnectAckMsg: public RemoteConnectMsg {
    RemoteConnectAckMsg(uint32_t pid = 0):RemoteConnectMsg(pid) {}
    
protected:
    uint32_t id() const override {
        return KRemoteConnectedAck;
    }
};

struct RemoteClosedMsg: public RemoteConnectMsg {
    
protected:
    uint32_t id() const override {
        return KRemoteClosed;
    }
};

struct RemoteLogMsg: public IPCMessage {
    std::string log_;
    
    RemoteLogMsg() {}
    RemoteLogMsg(const std::string& log):log_(log) {}
    RemoteLogMsg(const char* log):log_(log) {}
    
protected:
    uint32_t id() const override {
        return kLogMsgId;
    }
    
    uint32_t pack(BinaryPack& bin) const override{
        return bin.push(log_);
    }
    
    bool unpack(BinaryUnpack& bin) override{
        return bin.pop(log_);
    }
};
