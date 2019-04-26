/*
 ******************************************************************************
  Copyright (c) 2013-2015 Particle Industries, Inc.  All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, see <http://www.gnu.org/licenses/>.
  ******************************************************************************
 */

#include "spark_wiring_ble.h"

#if Wiring_BLE

#include "logging.h"
#include <memory>

LOG_SOURCE_CATEGORY("wiring.ble")

#undef DEBUG // Legacy logging macro

#if BLE_WIRING_DEBUG_ENABLED
#define DEBUG(_fmt, ...) \
        do { \
            LOG_PRINTF(TRACE, _fmt "\r\n", ##__VA_ARGS__); \
        } while (false)

#define DEBUG_DUMP(_data, _size) \
        do { \
            LOG_PRINT(TRACE, "> "); \
            LOG_DUMP(TRACE, _data, _size); \
            LOG_PRINTF(TRACE, " (%u bytes)\r\n", (unsigned)(_size)); \
        } while (false)
#else
#define DEBUG(...)
#define DEBUG_DUMP(...)
#endif


namespace {

const uint16_t PARTICLE_COMPANY_ID = 0x0662;

const uint8_t BLE_CTRL_REQ_SVC_UUID[BLE_SIG_UUID_128BIT_LEN] = {
    0xfc, 0x36, 0x6f, 0x54, 0x30, 0x80, 0xf4, 0x94, 0xa8, 0x48, 0x4e, 0x5c, 0x01, 0x00, 0xa9, 0x6f
};

void convertToHalUuid(const BleUuid& uuid, hal_ble_uuid_t* halUuid) {
    if (halUuid == nullptr) {
        return;
    }

    if (uuid.type() == BleUuidType::SHORT) {
        halUuid->type = BLE_UUID_TYPE_16BIT;
        halUuid->uuid16 = uuid.shorted();
    } else {
        halUuid->type = BLE_UUID_TYPE_128BIT;
        if (uuid.order() == BleUuidOrder::LSB) {
            uuid.full(halUuid->uuid128);
        } else {
            for (size_t i = 0, j = BLE_SIG_UUID_128BIT_LEN - 1; i < BLE_SIG_UUID_128BIT_LEN; i++, j--) {
                halUuid->uuid128[i] = uuid.full()[j];
            }
        }
    }
}

BleUuid convertToWiringUuid(const hal_ble_uuid_t* halUuid) {
    if (halUuid->type == BLE_UUID_TYPE_16BIT || halUuid->type == BLE_UUID_TYPE_128BIT_SHORTED) {
        BleUuid uuid(halUuid->uuid16);
        return uuid;
    } else {
        BleUuid uuid(halUuid->uuid128);
        return uuid;
    }
}

} //anonymous namespace


/*******************************************************
 * BleUuid class
 */
BleUuid::BleUuid() : type_(BleUuidType::SHORT), order_(BleUuidOrder::LSB), shortUUID_(0x0000) {
}

BleUuid::BleUuid(const BleUuid& uuid) {
    type_ = uuid.type();
    order_ = uuid.order();
    shortUUID_ = uuid.shorted();
    memcpy(fullUUID_, uuid.full(), BLE_SIG_UUID_128BIT_LEN);
}

BleUuid::BleUuid(const uint8_t* uuid128, BleUuidOrder order) : order_(order), shortUUID_(0x0000) {
    if (uuid128 == nullptr) {
        memset(fullUUID_, 0x00, BLE_SIG_UUID_128BIT_LEN);
    } else {
        memcpy(fullUUID_, uuid128, BLE_SIG_UUID_128BIT_LEN);
    }
    type_ = BleUuidType::LONG;
}

BleUuid::BleUuid(uint16_t uuid16, BleUuidOrder order) : order_(order), shortUUID_(uuid16) {
    type_ = BleUuidType::SHORT;
    memset(fullUUID_, 0x00, BLE_SIG_UUID_128BIT_LEN);
}

BleUuid::BleUuid(const uint8_t* uuid128, uint16_t uuid16, BleUuidOrder order) : order_(order), shortUUID_(0x0000) {
    type_ = BleUuidType::LONG;
    if (uuid128 == nullptr) {
        memset(fullUUID_, 0x00, BLE_SIG_UUID_128BIT_LEN);
    } else {
        memcpy(fullUUID_, uuid128, BLE_SIG_UUID_128BIT_LEN);
    }
    if (order == BleUuidOrder::LSB) {
        fullUUID_[12] = (uint8_t)(uuid16 & 0x00FF);
        fullUUID_[13] = (uint8_t)((uuid16 >> 8) & 0x00FF);
    } else {
        fullUUID_[13] = (uint8_t)(uuid16 & 0x00FF);
        fullUUID_[12] = (uint8_t)((uuid16 >> 8) & 0x00FF);
    }
}

BleUuid::BleUuid(const String& uuid) : type_(BleUuidType::LONG), order_(BleUuidOrder::LSB), shortUUID_(0x0000) {
    setUuid(uuid);
}

BleUuid::BleUuid(const char* uuid) : type_(BleUuidType::LONG), order_(BleUuidOrder::LSB), shortUUID_(0x0000) {
    if (uuid == nullptr) {
        memset(fullUUID_, 0x00, BLE_SIG_UUID_128BIT_LEN);
    } else {
        String str(uuid);
        setUuid(str);
    }
}

BleUuid::~BleUuid() {

}

bool BleUuid::isValid(void) const {
    if (type_ == BleUuidType::SHORT) {
        return shortUUID_ != 0x0000;
    } else {
        uint8_t temp[BLE_SIG_UUID_128BIT_LEN];
        memset(temp, 0x00, sizeof(temp));
        return memcmp(fullUUID_, temp, BLE_SIG_UUID_128BIT_LEN);
    }
}

bool BleUuid::operator == (const BleUuid& uuid) const {
    if (type_ == uuid.type() && order_ == uuid.order()) {
        if (type_ == BleUuidType::SHORT) {
            return (shortUUID_ == uuid.shorted());
        } else {
            return !memcmp(full(), uuid.full(), BLE_SIG_UUID_128BIT_LEN);
        }
    }
    return false;
}

int8_t BleUuid::toInt(char c) {
    if (c >= '0' && c <= '9') {
        return (c - '0');
    } else if (c >= 'a' && c <= 'f') {
        return (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
        return (c - 'A' + 10);
    } else {
        return -1;
    }
}

void BleUuid::setUuid(const String& str) {
    size_t len = BLE_SIG_UUID_128BIT_LEN;
    for (size_t i = 0; i < str.length(); i++) {
        int8_t hi = toInt(str.charAt(i));
        if (hi >= 0) {
            fullUUID_[len - 1] = hi << 4;
            if (++i < str.length()) {
                int8_t lo = toInt(str.charAt(i));
                if (lo >= 0) {
                    fullUUID_[len - 1] |= lo;
                }
            }
            len--;
        }
    }
    while (len > 0) {
        fullUUID_[len - 1] = 0x00;
        len--;
    }
}


/*******************************************************
 * BleAdvertisingData class
 */
BleAdvertisingData::BleAdvertisingData() : selfLen_(0) {
    memset(selfData_, 0x00, sizeof(selfData_));
}

BleAdvertisingData::~BleAdvertisingData() {
}

size_t BleAdvertisingData::set(const uint8_t* buf, size_t len) {
    if (buf == nullptr) {
        return selfLen_;
    }
    len = len >= BLE_MAX_ADV_DATA_LEN ? BLE_MAX_ADV_DATA_LEN : len;
    memcpy(selfData_, buf, len);
    selfLen_ = len;
    return selfLen_;
}

size_t BleAdvertisingData::append(uint8_t type, const uint8_t* buf, size_t len, bool force) {
    if (buf == nullptr) {
        return selfLen_;
    }
    size_t offset;
    size_t adsLen = locate(selfData_, selfLen_, type, &offset);
    if (!force && adsLen > 0) {
        // Update the existing AD structure.
        uint16_t staLen = selfLen_ - adsLen;
        if ((staLen + len + 2) <= BLE_MAX_ADV_DATA_LEN) {
            // Firstly, move the last consistent block.
            uint16_t moveLen = selfLen_ - offset - adsLen;
            memmove(&selfData_[offset + len + 2], &selfData_[offset + adsLen], moveLen);
            // Secondly, Update the AD structure.
            // The Length field is the total length of Type field and Data field.
            selfData_[offset] = len + 1;
            memcpy(&selfData_[offset + 2], buf, len);
            // An AD structure is composed of one byte length field, one byte Type field and Data field.
            selfLen_ = staLen + len + 2;
        }
    }
    else if ((selfLen_ + len + 2) <= BLE_MAX_ADV_DATA_LEN) {
        // Append the AD structure at the and of advertising data.
        selfData_[selfLen_++] = len + 1;
        selfData_[selfLen_++] = type;
        memcpy(&selfData_[selfLen_], buf, len);
        selfLen_ += len;
    }
    return selfLen_;
}

size_t BleAdvertisingData::appendLocalName(const char* name, bool force) {
    return append(BLE_SIG_AD_TYPE_COMPLETE_LOCAL_NAME, (const uint8_t*)name, strlen(name), force);
}

size_t BleAdvertisingData::appendCustomData(const uint8_t* buf, size_t len, bool force) {
    return append(BLE_SIG_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buf, len, force);
}

void BleAdvertisingData::clear(void) {
    selfLen_ = 0;
    memset(selfData_, 0x00, sizeof(selfData_));
}

void BleAdvertisingData::remove(uint8_t type) {
    size_t offset, len;
    len = locate(selfData_, selfLen_, type, &offset);
    if (len > 0) {
        uint16_t moveLen = selfLen_ - offset - len;
        memcpy(&selfData_[offset], &selfData_[offset + len], moveLen);
        selfLen_ -= len;
        // Recursively remove duplicated type.
        remove(type);
    }
}

size_t BleAdvertisingData::get(uint8_t* buf, size_t len) const {
    if (buf != nullptr) {
        len = len > selfLen_ ? selfLen_ : len;
        memcpy(buf, selfData_, len);
        return len;
    }
    return selfLen_;
}

size_t BleAdvertisingData::get(uint8_t type, uint8_t* buf, size_t len) const {
    size_t offset;
    size_t adsLen = locate(selfData_, selfLen_, type, &offset);
    if (adsLen > 0) {
        if ((adsLen - 2) > 0) {
            adsLen -= 2;
            len = len > adsLen ? adsLen : len;
            if (buf != nullptr) {
                memcpy(buf, &selfData_[offset + 2], len);
            }
            return len;
        }
    }
    return 0;
}

const uint8_t* BleAdvertisingData::data(void) const {
    return selfData_;
}

size_t BleAdvertisingData::length(void) const {
    return selfLen_;
}

size_t BleAdvertisingData::deviceName(uint8_t* buf, size_t len) const {
    size_t nameLen = get(BLE_SIG_AD_TYPE_SHORT_LOCAL_NAME, buf, len);
    if (nameLen > 0) {
        return nameLen;
    }
    return get(BLE_SIG_AD_TYPE_COMPLETE_LOCAL_NAME, buf, len);
}

String BleAdvertisingData::deviceName(void) const {
    String name;
    char buf[BLE_MAX_ADV_DATA_LEN];
    size_t len = deviceName((uint8_t*)buf, sizeof(buf));
    if (len > 0) {
        for (size_t i = 0; i < len; i++) {
            if (!name.concat(buf[i])) {
                break;
            }
        }
    }
    return name;
}

size_t BleAdvertisingData::serviceUUID(BleUuid* uuids, size_t count) const {
    size_t found = 0;
    found += serviceUUID(BLE_SIG_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE, &uuids[found], count - found);
    found += serviceUUID(BLE_SIG_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE, &uuids[found], count - found);
    found += serviceUUID(BLE_SIG_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE, &uuids[found], count - found);
    found += serviceUUID(BLE_SIG_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE, &uuids[found], count - found);
    return found;
}

size_t BleAdvertisingData::customData(uint8_t* buf, size_t len) const {
    return get(BLE_SIG_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buf, len);
}

bool BleAdvertisingData::contains (uint8_t type) const {
    size_t adsOffset;
    return locate(selfData_, selfLen_, type, &adsOffset) > 0;
}

size_t BleAdvertisingData::serviceUUID(uint8_t type, BleUuid* uuids, size_t count) const {
    size_t offset, adsLen = 0, found = 0;
    for (size_t i = 0; i < selfLen_; i += (offset + adsLen)) {
        adsLen = locate(&selfData_[i], selfLen_ - i, type, &offset);
        if (adsLen > 0 && found < count) {
            if (adsLen == 4) { // length field + type field + 16-bits UUID
                uint16_t temp = (uint16_t)selfData_[i + offset + 2] | ((uint16_t)selfData_[i + offset + 3] << 8);
                BleUuid uuid(temp);
                uuids[found++] = uuid;
            } else if (adsLen == 18) {
                BleUuid uuid(&selfData_[i + offset + 2]);
                uuids[found++] = uuid;
            }
            continue;
        }
        break;
    }
    return found;
}

size_t BleAdvertisingData::locate(const uint8_t* buf, size_t len, uint8_t type, size_t* offset) {
    if (offset == nullptr) {
        return 0;
    }

    size_t adsLen;
    for (size_t i = 0; (i + 3) <= len; i = i) {
        adsLen = buf[i];
        if (buf[i + 1] == type) {
            // The value of adsLen doesn't include the length field of an AD structure.
            if ((i + adsLen + 1) <= len) {
                *offset = i;
                adsLen += 1;
                return adsLen;
            } else {
                return 0;
            }
        } else {
            // Navigate to the next AD structure.
            i += (adsLen + 1);
        }
    }
    return 0;
}


class BleCharacteristicRef {
public:
    BleCharacteristic* charPtr;
    bool isStub;

    BleCharacteristicRef() {
        charPtr = nullptr;
        isStub = false;
    }

    BleCharacteristicRef(BleCharacteristic* characteristic, bool stub)
        : charPtr(characteristic),
          isStub(stub) {
    }

    ~BleCharacteristicRef() {
    }

    bool operator == (const BleCharacteristicRef& reference) {
        return (reference.charPtr == this->charPtr);
    }
};


/*******************************************************
 * BleCharacteristicImpl definition
 */
class BleCharacteristicImpl {
public:
    BleCharacteristicProperties properties;
    BleUuid uuid;
    BleUuid svcUuid;
    String description;
    bool isLocal;
    BleCharacteristicHandles attrHandles;
    OnDataReceivedCallback dataCb;
    BleConnectionHandle connHandle; // For peer characteristic
    BleServiceImpl* svcImpl; // Related service
    Vector<BleCharacteristicRef> references;

    BleCharacteristicImpl() {
        init();
    }

    BleCharacteristicImpl(const char* desc, BleCharacteristicProperties properties, OnDataReceivedCallback callback) {
        init();
        this->description = desc;
        this->properties = properties;
        this->dataCb = callback;
    }

    BleCharacteristicImpl(const char* desc, BleCharacteristicProperties properties, BleUuid& charUuid, BleUuid& svcUuid, OnDataReceivedCallback callback) {
        init();
        this->description = desc;
        this->properties = properties;
        this->uuid = charUuid;
        this->svcUuid = svcUuid;
        this->dataCb = callback;
    }

    ~BleCharacteristicImpl() {
    }

    void init(void) {
        properties = PROPERTY::NONE;
        description = "";
        isLocal = true;
        connHandle = BLE_INVALID_CONN_HANDLE;
        svcImpl = nullptr;
        dataCb = nullptr;
    }

    size_t getValue(uint8_t* buf, size_t len) const {
        if (buf == nullptr) {
            return 0;
        }
        len = len > BLE_MAX_ATTR_VALUE_PACKET_SIZE ? BLE_MAX_ATTR_VALUE_PACKET_SIZE : len;
        int ret;
        if (isLocal) {
            ret = ble_gatt_server_get_characteristic_value(attrHandles.value_handle, buf, len, nullptr);
        } else {
            ret = ble_gatt_client_read(connHandle, attrHandles.value_handle, buf, len, nullptr);
        }
        if (ret == SYSTEM_ERROR_NONE) {
            return len;
        }
        return 0;
    }

    size_t setValue(const uint8_t* buf, size_t len) {
        if (buf == nullptr) {
            return 0;
        }
        len = len > BLE_MAX_ATTR_VALUE_PACKET_SIZE ? BLE_MAX_ATTR_VALUE_PACKET_SIZE : len;
        int ret = 0;
        if (isLocal) {
            ret = ble_gatt_server_set_characteristic_value(attrHandles.value_handle, buf, len, nullptr);
            if (ret == 0) {
                LOG(ERROR, "ble_gatt_server_set_characteristic_value failed.");
            }
        } else {
            if (properties & PROPERTY::WRITE) {
                ret = ble_gatt_client_write_with_response(connHandle, attrHandles.value_handle, buf, len, nullptr);
            } else if (properties & PROPERTY::WRITE_WO_RSP) {
                ret = ble_gatt_client_write_without_response(connHandle, attrHandles.value_handle, buf, len, nullptr);
            }
        }
        return ret;
    }

    void configureCccd(BleConnectionHandle handle, uint8_t enable) {
        if (!isLocal) {
            // Gatt Client configure peer CCCD.
        }
    }

    void assignUuidIfNeeded(void) {
        if (!uuid.isValid()) {
            defaultUuidCharCount_++;
            BleUuid newUuid(BLE_CTRL_REQ_SVC_UUID, defaultUuidCharCount_);
            uuid = newUuid;
        }
    }

    void addReference(BleCharacteristic* characteristic, bool stub) {
        BleCharacteristicRef reference(characteristic, stub);
        references.append(reference);
        DEBUG(TRACE, "0x%08X added references: %d", this, references.size());
    }

    void removeReference(BleCharacteristic* characteristic) {
        for (int i = 0; i < references.size(); i++) {
            BleCharacteristicRef& reference = references[i];
            if (reference.charPtr == characteristic) {
                references.removeOne(reference);
                DEBUG(TRACE, "0x%08X removed references: %d", this, references.size());
            }
        }
    }

    bool isReferenceStub(const BleCharacteristic* characteristic) {
        for (int i = 0; i < references.size(); i++) {
            BleCharacteristicRef& reference = references[i];
            if (reference.charPtr == characteristic) {
                return reference.isStub;
            }
        }
        return false;
    }

    BleCharacteristic* getReferenceStub(void) {
        for (int i = 0; i < references.size(); i++) {
            BleCharacteristicRef& reference = references[i];
            if (reference.isStub) {
                return reference.charPtr;
            }
        }
        return nullptr;
    }

    size_t referenceStubCount(void) {
        size_t total = 0;
        for (int i = 0; i < references.size(); i++) {
            BleCharacteristicRef& reference = references[i];
            if (reference.isStub) {
                total++;
            }
        }
        return total;
    }

    void markReferenceAsStub(BleCharacteristic* characteristic) {
        for (int i = 0; i < references.size(); i++) {
            BleCharacteristicRef& reference = references[i];
            if (reference.charPtr == characteristic) {
                reference.isStub = true;
                DEBUG(TRACE, "0x%08X STUB references: 0x%08X", this, characteristic);
            }
        }
    }

    void processReceivedData(BleAttributeHandle attrHandle, const uint8_t* data, size_t len, const BlePeerDevice& peer) {
        if (data == nullptr) {
            return;
        }
        BleCharacteristic* characteristic = getReferenceStub();
        if (characteristic && dataCb && attrHandle == attrHandles.value_handle) {
            dataCb(data, len, peer, characteristic);
        }
    }

private:
    static uint16_t defaultUuidCharCount_;
};

uint16_t BleCharacteristicImpl::defaultUuidCharCount_ = 0;


/*******************************************************
 * BleCharacteristic class
 */
BleCharacteristic::BleCharacteristic()
    : impl_(std::make_shared<BleCharacteristicImpl>()) {
    impl()->addReference(this, false);
    DEBUG(TRACE, "BleCharacteristic::BleCharacteristic():0x%08X -> 0x%08X", this, impl());
    DEBUG(TRACE, "shared_ptr count: %d", impl_.use_count());
}

BleCharacteristic::BleCharacteristic(const BleCharacteristic& characteristic)
    : impl_(characteristic.impl_) {
    DEBUG(TRACE, "BleCharacteristic::BleCharacteristic(copy):0x%08X => 0x%08X -> 0x%08X", &characteristic, this, impl());
    DEBUG(TRACE, "shared_ptr count: %d", impl_.use_count());
    if (impl() != nullptr) {
        impl()->addReference(this, impl()->isReferenceStub(&characteristic));
    }
}

BleCharacteristic::BleCharacteristic(const char* desc, BleCharacteristicProperties properties, OnDataReceivedCallback callback)
    : impl_(std::make_shared<BleCharacteristicImpl>(desc, properties, callback)) {
    impl()->addReference(this, false);

    DEBUG(TRACE, "BleCharacteristic::BleCharacteristic(...):0x%08X -> 0x%08X", this, impl());
    DEBUG(TRACE, "shared_ptr count: %d", impl_.use_count());
}

void BleCharacteristic::construct(const char* desc, BleCharacteristicProperties properties, BleUuid& charUuid, BleUuid& svcUuid, OnDataReceivedCallback callback) {
    impl_ = std::make_shared<BleCharacteristicImpl>(desc, properties, charUuid, svcUuid, callback);
    impl()->addReference(this, false);

    DEBUG(TRACE, "BleCharacteristic::construct(...):0x%08X -> 0x%08X", this, impl());
    DEBUG(TRACE, "shared_ptr count: %d", impl_.use_count());
}

BleCharacteristic& BleCharacteristic::operator=(const BleCharacteristic& characteristic) {
    DEBUG(TRACE, "BleCharacteristic::operator=:0x%08X -> 0x%08X", this, impl());
    DEBUG(TRACE, "shared_ptr pre-count1: %d", impl_.use_count());
    OnDataReceivedCallback preDataCb = nullptr;
    if (impl() != nullptr) {
        if (impl()->dataCb != nullptr) {
            preDataCb = impl()->dataCb;
        }
        if (impl()->isReferenceStub(this) && impl()->referenceStubCount() == 1) {
            for (int i = 0; i < impl()->references.size(); i++) {
                BleCharacteristicRef reference = impl()->references[i];
                if (reference.charPtr != this) {
                    reference.charPtr->impl_ = nullptr;
                }
            }
            DEBUG(TRACE, "shared_ptr pre-count2: %d", impl_.use_count());
        } else {
            impl()->removeReference(this);
        }
    }

    impl_ = characteristic.impl_;
    if (impl()->dataCb == nullptr) {
        impl()->dataCb = preDataCb;
    }
    DEBUG(TRACE, "now:0x%08X -> 0x%08X", this, impl());
    DEBUG(TRACE, "shared_ptr curr-count: %d", impl_.use_count());
    if (impl() != nullptr) {
        impl()->addReference(this, impl()->isReferenceStub(&characteristic));
    }
    return *this;
}

BleCharacteristic::~BleCharacteristic() {
    DEBUG(TRACE, "BleCharacteristic::~BleCharacteristic:0x%08X -> 0x%08X", this, impl());
    if (impl() != nullptr) {
        if (impl()->isReferenceStub(this) && impl()->referenceStubCount() == 1) {
            DEBUG(TRACE, "shared_ptr count1: %d", impl_.use_count());
            for (int i = 0; i < impl()->references.size(); i++) {
                BleCharacteristicRef reference = impl()->references[i];
                if (reference.charPtr != this) {
                    reference.charPtr->impl_ = nullptr;
                }
            }
            DEBUG(TRACE, "shared_ptr count2: %d", impl_.use_count());
        } else {
            DEBUG(TRACE, "shared_ptr count1: %d", impl_.use_count());
            impl()->removeReference(this);
        }
    }
}

BleUuid BleCharacteristic::UUID(void) const {
    if (impl() != nullptr) {
        return impl()->uuid;
    }
    BleUuid uuid((uint16_t)0x0000);
    return uuid;
}

BleCharacteristicProperties BleCharacteristic::properties(void) const {
    if (impl() != nullptr) {
        return impl()->properties;
    }
    return PROPERTY::NONE;
}

size_t BleCharacteristic::setValue(const uint8_t* buf, size_t len) {
    if (impl() != nullptr) {
        return impl()->setValue(buf, len);
    }
    return 0;
}

size_t BleCharacteristic::setValue(const String& str) {
    return setValue(reinterpret_cast<const uint8_t*>(str.c_str()), str.length());
}

size_t BleCharacteristic::setValue(const char* str) {
    return setValue(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

size_t BleCharacteristic::getValue(uint8_t* buf, size_t len) const {
    if (impl() != nullptr) {
        return impl()->getValue(buf, len);
    }
    return 0;
}

size_t BleCharacteristic::getValue(String& str) const {
    return 0;
}

void BleCharacteristic::onDataReceived(OnDataReceivedCallback callback) {
    if (impl() != nullptr) {
        impl()->dataCb = callback;
    }
}


/*******************************************************
 * BleServiceImpl definition
 */
class BleServiceImpl {
public:
    BleUuid uuid;
    BleAttributeHandle startHandle;
    BleAttributeHandle endHandle;
    Vector<BleCharacteristic> characteristics;
    BleGattServerImpl* gattsProxy; // Related GATT Server

    BleServiceImpl() {
        init();
    }

    BleServiceImpl(const BleUuid& uuid)
        : uuid(uuid) {
        init();
    }

    ~BleServiceImpl() {
    }

    void init(void) {
        startHandle = 0;
        endHandle = 0;
        gattsProxy = nullptr;
    }

    size_t characteristicCount(void) const {
        return characteristics.size();
    }

    bool contains(const BleCharacteristic& characteristic) {
        if (characteristic.impl() != nullptr) {
            for (size_t i = 0; i < characteristicCount(); i++) {
                BleCharacteristic& stubChar = characteristics[i];
                if (characteristic.impl() == stubChar.impl()) {
                    return true;
                }
            }
        }
        return false;
    }

    BleCharacteristic* characteristic(const char* desc) {
        if (desc == nullptr) {
            return nullptr;
        }
        for (size_t i = 0; i < characteristicCount(); i++) {
            BleCharacteristic& characteristic = characteristics[i];
            if (characteristic.impl() != nullptr && !strcmp(characteristic.impl()->description.c_str(), desc)) {
                return &characteristic;
            }
        }
        return nullptr;
    }

    BleCharacteristic* characteristic(BleAttributeHandle attrHandle) {
        for (size_t i = 0; i < characteristicCount(); i++) {
            BleCharacteristic& characteristic = characteristics[i];
            BleCharacteristicImpl* charImpl = characteristic.impl();
            if (charImpl != nullptr) {
                if (   charImpl->attrHandles.decl_handle == attrHandle
                    || charImpl->attrHandles.value_handle == attrHandle
                    || charImpl->attrHandles.user_desc_handle == attrHandle
                    || charImpl->attrHandles.cccd_handle == attrHandle
                    || charImpl->attrHandles.sccd_handle == attrHandle) {
                    return &characteristic;
                }
            }
        }
        return nullptr;
    }

    BleCharacteristic* characteristic(const BleUuid& charUuid) {
        for (size_t i = 0; i < characteristicCount(); i++) {
            BleCharacteristic& characteristic = characteristics[i];
            if (characteristic.impl() != nullptr && characteristic.impl()->uuid == charUuid) {
                return &characteristic;
            }
        }
        return nullptr;
    }

    int addCharacteristic(BleCharacteristic& characteristic) {
        BleCharacteristicImpl* charImpl = characteristic.impl();
        if (charImpl == nullptr || contains(characteristic) || !charImpl->properties) {
            return SYSTEM_ERROR_INVALID_ARGUMENT;
        }
        if (characteristic.impl()->isLocal) {
            characteristic.impl()->assignUuidIfNeeded();
            hal_ble_char_init_t char_init;
            memset(&char_init, 0x00, sizeof(hal_ble_char_init_t));
            convertToHalUuid(characteristic.impl()->uuid, &char_init.uuid);
            char_init.properties = static_cast<uint8_t>(characteristic.impl()->properties);
            char_init.service_handle = startHandle;
            char_init.description = characteristic.impl()->description.c_str();
            int ret = ble_gatt_server_add_characteristic(&char_init, &characteristic.impl()->attrHandles, nullptr);
            if (ret != SYSTEM_ERROR_NONE) {
                return ret;
            }
        }
        characteristic.impl()->svcImpl = this;
        LOG_DEBUG(TRACE, "characteristics.append(characteristic)");
        characteristics.append(characteristic);
        BleCharacteristic& lastChar = characteristics.last();
        lastChar.impl()->markReferenceAsStub(&lastChar);
        return SYSTEM_ERROR_NONE;
    }
};


/*******************************************************
 * BleService class
 */
BleService::BleService()
    : impl_(std::make_shared<BleServiceImpl>()) {
}

BleService::BleService(const BleUuid& uuid)
    : impl_(std::make_shared<BleServiceImpl>(uuid)) {
}

BleService::~BleService() {
}


/*******************************************************
 * BleGattServerImpl definition
 */
class BleGattServerImpl {
public:
    Vector<BleService> services;

    // Related device
    BleAddress address;

    BleGattServerImpl(const BleAddress& addr)
        : address(addr) {
    }

    ~BleGattServerImpl() {
    }

    size_t serviceCount(void) const {
        return services.size();
    }

    BleService* service(const BleUuid& uuid) {
        for (size_t i = 0; i < serviceCount(); i++) {
            BleService& stubSvc = services[i];
            if (stubSvc.impl()->uuid == uuid) {
                return &stubSvc;
            }
        }
        return nullptr;
    }

    bool isLocal(void) {
        BleAddress addr;
        ble_gap_get_device_address(&addr);
        return addr == address;
    }

    int addService(BleService& svc) {
        if (service(svc.impl()->uuid) != nullptr) {
            return SYSTEM_ERROR_INVALID_ARGUMENT;
        }
        if (isLocal()) {
            hal_ble_uuid_t halUuid;
            convertToHalUuid(svc.impl()->uuid, &halUuid);
            int ret = ble_gatt_server_add_service(BLE_SERVICE_TYPE_PRIMARY, &halUuid, &svc.impl()->startHandle, nullptr);
            if (ret != SYSTEM_ERROR_NONE) {
                return ret;
            }
        }
        LOG_DEBUG(TRACE, "services.append(service)");
        services.append(svc);
        return SYSTEM_ERROR_NONE;
    }

    int addCharacteristic(BleCharacteristic& characteristic) {
        if (characteristic.impl() == nullptr) {
            return SYSTEM_ERROR_INVALID_ARGUMENT;
        }
        characteristic.impl()->isLocal = isLocal();
        if (isLocal()) {
            LOG_DEBUG(TRACE, "< LOCAL CHARACTERISTIC >");
            if (!characteristic.impl()->svcUuid.isValid()) {
                BleUuid newUuid(BLE_CTRL_REQ_SVC_UUID);
                characteristic.impl()->svcUuid = newUuid;
            }
        }
        BleService* stubSvc = service(characteristic.impl()->svcUuid);
        if (stubSvc != nullptr) {
            if (stubSvc->impl() != nullptr) {
                return stubSvc->impl()->addCharacteristic(characteristic);
            }
            return SYSTEM_ERROR_INTERNAL;
        } else {
            BleService service(characteristic.impl()->svcUuid);
            if (addService(service) == SYSTEM_ERROR_NONE) {
                return services.last().impl()->addCharacteristic(characteristic);
            }
            return SYSTEM_ERROR_INTERNAL;
        }
    }

    template <typename T>
    BleCharacteristic characteristic(T type) const {
        for (size_t i = 0; i < serviceCount(); i++) {
            BleCharacteristic* characteristic = services[i].impl()->characteristic(type);
            if (characteristic != nullptr) {
                return *characteristic;
            }
        }
        BleCharacteristic temp;
        return temp;
    }

    void gattsProcessDisconnected(const BlePeerDevice& peer) {
    }

    void gattsProcessDataWritten(BleAttributeHandle attrHandle, const uint8_t* buf, size_t len, BlePeerDevice& peer) {
        for (size_t i = 0; i < serviceCount(); i++) {
            BleCharacteristic* characteristic = services[i].impl()->characteristic(attrHandle);
            if (characteristic != nullptr) {
                characteristic->impl()->processReceivedData(attrHandle, buf, len, peer);
            }
        }
    }
};


/*******************************************************
 * BleGattClientImpl definition
 */
class BleGattClientImpl {
public:
    BleGattClientImpl() {
    }

    ~BleGattClientImpl() {
    }

    int discoverAllServices(BlePeerDevice& peer) {
        int ret = ble_gatt_client_discover_all_services(peer.connHandle, onServicesDiscovered, &peer, nullptr);
        if (ret == SYSTEM_ERROR_NONE) {
            for (int i = 0; i < peer.gattsProxy()->services.size(); i++) {
                LOG_DEBUG(TRACE, "Discovering characteristics of service %d.", i);
                BleService& service = peer.gattsProxy()->services[i];
                hal_ble_svc_t halService;
                halService.start_handle = service.impl()->startHandle;
                halService.end_handle = service.impl()->endHandle;
                ret = ble_gatt_client_discover_characteristics(peer.connHandle, &halService, onCharacteristicsDiscovered, &service, nullptr);
                if (ret != SYSTEM_ERROR_NONE) {
                    return ret;
                }
                for (int j = 0; j < service.impl()->characteristics.size(); j++) {
                    BleCharacteristic& characteristic = service.impl()->characteristics[j];
                    // Enable notification or indication if presented.
                    if (characteristic.impl()->attrHandles.cccd_handle != BLE_INVALID_ATTR_HANDLE) {
                        LOG_DEBUG(TRACE, "Enable CCCD of characteristics: %d.", j);
                        if (characteristic.impl()->properties & PROPERTY::NOTIFY) {
                            ble_gatt_client_configure_cccd(peer.connHandle, characteristic.impl()->attrHandles.cccd_handle, BLE_SIG_CCCD_VAL_NOTIFICATION, nullptr);
                        } else if (characteristic.impl()->properties & PROPERTY::INDICATE) {
                            ble_gatt_client_configure_cccd(peer.connHandle, characteristic.impl()->attrHandles.cccd_handle, BLE_SIG_CCCD_VAL_INDICATION, nullptr);
                        }
                    }
                    // Read the user description string if presented.
                    if (characteristic.impl()->attrHandles.user_desc_handle != BLE_INVALID_ATTR_HANDLE) {
                        LOG_DEBUG(TRACE, "Read user description of characteristics: %d.", j);
                        char desc[32]; // FIXME: use macro definition instead.
                        size_t len = ble_gatt_client_read(peer.connHandle, characteristic.impl()->attrHandles.user_desc_handle, (uint8_t*)desc, sizeof(desc) - 1, nullptr);
                        if (len > 0) {
                            desc[len] = '\0';
                            characteristic.impl()->description = desc;
                            LOG_DEBUG(TRACE, "User description: %s.", desc);
                        }
                    }
                }
            }
        }
        return ret;
    }

    void gattcProcessDataNotified(BleAttributeHandle attrHandle, const uint8_t* buf, size_t len, BlePeerDevice& peer) {
        peer.gattsProxy()->gattsProcessDataWritten(attrHandle, buf, len, peer);
    }

    static void onServicesDiscovered(const hal_ble_gattc_on_svc_disc_evt_t* event, void* context) {
        BlePeerDevice* peer = static_cast<BlePeerDevice*>(context);
        for (uint8_t i = 0; i < event->count; i++) {
            BleUuid svcUUID = convertToWiringUuid(&event->services[i].uuid);
            BleService service(svcUUID);
            service.impl()->startHandle = event->services[i].start_handle;
            service.impl()->endHandle = event->services[i].end_handle;
            if (peer->gattsProxy()->addService(service) == SYSTEM_ERROR_NONE) {
                LOG_DEBUG(TRACE, "New service found.");
            }
        }
    }

    static void onCharacteristicsDiscovered(const hal_ble_gattc_on_char_disc_evt_t* event, void* context) {
        BleService* service = static_cast<BleService*>(context);
        for (uint8_t i = 0; i < event->count; i++) {
            BleCharacteristic characteristic;
            characteristic.impl()->isLocal = false;
            characteristic.impl()->connHandle = event->conn_handle;
            characteristic.impl()->svcUuid = service->impl()->uuid;
            if (event->characteristics[i].properties & BLE_SIG_CHAR_PROP_READ) {
                characteristic.impl()->properties |= PROPERTY::READ;
            }
            if (event->characteristics[i].properties & BLE_SIG_CHAR_PROP_WRITE_WO_RESP) {
                characteristic.impl()->properties |= PROPERTY::WRITE_WO_RSP;
            }
            if (event->characteristics[i].properties & BLE_SIG_CHAR_PROP_WRITE) {
                characteristic.impl()->properties |= PROPERTY::WRITE;
            }
            if (event->characteristics[i].properties & BLE_SIG_CHAR_PROP_NOTIFY){
                characteristic.impl()->properties |= PROPERTY::NOTIFY;
            }
            if (event->characteristics[i].properties & BLE_SIG_CHAR_PROP_INDICATE) {
                characteristic.impl()->properties |= PROPERTY::INDICATE;
            }
            BleUuid charUUID = convertToWiringUuid(&event->characteristics[i].uuid);
            characteristic.impl()->uuid = charUUID;
            characteristic.impl()->attrHandles = event->characteristics[i].attr_handles;
            if (service->impl()->addCharacteristic(characteristic) == SYSTEM_ERROR_NONE) {
                LOG_DEBUG(TRACE, "New characteristic found.");
            }
        }
    }

private:

};


/*******************************************************
 * BleBroadcasterImpl definition
 */
class BleBroadcasterImpl {
public:
    BleBroadcasterImpl() {}
    ~BleBroadcasterImpl() {}

    int setTxPower(int8_t val) const {
        return ble_gap_set_tx_power(val);
    }

    int8_t txPower(void) const {
        return ble_gap_get_tx_power();
    }

    int setAdvertisingInterval(uint16_t interval) {
        hal_ble_adv_params_t advParams;
        int ret = ble_gap_get_advertising_parameters(&advParams, nullptr);
        if (ret == SYSTEM_ERROR_NONE) {
            advParams.interval = interval;
            ret = ble_gap_set_advertising_parameters(&advParams, nullptr);
        }
        return ret;
    }

    int setAdvertisingTimeout(uint16_t timeout) {
        hal_ble_adv_params_t advParams;
        int ret = ble_gap_get_advertising_parameters(&advParams, nullptr);
        if (ret == SYSTEM_ERROR_NONE) {
            advParams.timeout = timeout;
            ret = ble_gap_set_advertising_parameters(&advParams, nullptr);
        }
        return ret;
    }

    int setAdvertisingType(uint8_t type) {
        hal_ble_adv_params_t advParams;
        int ret = ble_gap_get_advertising_parameters(&advParams, nullptr);
        if (ret == SYSTEM_ERROR_NONE) {
            advParams.type = type;
            ret = ble_gap_set_advertising_parameters(&advParams, nullptr);
        }
        return ret;
    }

    int setAdvertisingParams(const BleAdvertisingParams* params) {
        return ble_gap_set_advertising_parameters(params, nullptr);
    }

    int setIbeacon(const iBeacon& iBeacon) {
        if (iBeacon.uuid.type() == BleUuidType::SHORT) {
            return SYSTEM_ERROR_INVALID_ARGUMENT;
        }

        BleAdvertisingData advertisingData;
        advertisingData.clear();

        // AD flags
        uint8_t flags = BLE_SIG_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
        advertisingData.append(BLE_SIG_AD_TYPE_FLAGS, &flags, 1);

        uint8_t mfgData[BLE_MAX_ADV_DATA_LEN];
        size_t mfgDataLen = 0;

        // Company ID
        memcpy(&mfgData[mfgDataLen], (uint8_t*)&iBeacon::APPLE_COMPANY_ID, sizeof(iBeacon::APPLE_COMPANY_ID));
        mfgDataLen += sizeof(iBeacon::APPLE_COMPANY_ID);
        // Beacon type: iBeacon
        mfgData[mfgDataLen++] = iBeacon::BEACON_TYPE_IBEACON;
        // Length of the following payload
        mfgData[mfgDataLen++] = 0x15;
        // 128-bits Beacon UUID, MSB
        if (iBeacon.uuid.order() == BleUuidOrder::MSB) {
            memcpy(&mfgData[mfgDataLen], iBeacon.uuid.full(), BLE_SIG_UUID_128BIT_LEN);
        } else {
            for (size_t i = 0, j = BLE_SIG_UUID_128BIT_LEN - 1; i < BLE_SIG_UUID_128BIT_LEN; i++, j--) {
                mfgData[mfgDataLen + i] = iBeacon.uuid.full()[j];
            }
        }
        mfgDataLen += BLE_SIG_UUID_128BIT_LEN;
        // Major, MSB
        mfgData[mfgDataLen++] = (uint8_t)((iBeacon.major >> 8) & 0x00FF);
        mfgData[mfgDataLen++] = (uint8_t)(iBeacon.major & 0x00FF);
        // Minor, MSB
        mfgData[mfgDataLen++] = (uint8_t)((iBeacon.minor >> 8) & 0x00FF);
        mfgData[mfgDataLen++] = (uint8_t)(iBeacon.minor & 0x00FF);
        // Measure power
        mfgData[mfgDataLen++] = iBeacon.measurePower;
        advertisingData.appendCustomData(mfgData, mfgDataLen);

        int ret = ble_gap_set_advertising_data(advertisingData.data(), advertisingData.length(), nullptr);
        if (ret == SYSTEM_ERROR_NONE) {
            // Clear the scan response data.
            ret = ble_gap_set_scan_response_data(nullptr, 0, nullptr);
        }
        return ret;
    }

    int setAdvertisingData(const BleAdvertisingData* advData) {
        int ret;
        if (advData == nullptr) {
            ret = ble_gap_set_advertising_data(nullptr, 0, nullptr);
        } else {
            if (advData->contains(BLE_SIG_AD_TYPE_FLAGS)) {
                ret = ble_gap_set_advertising_data(advData->data(), advData->length(), nullptr);
            } else {
                uint8_t temp[BLE_MAX_ADV_DATA_LEN];
                ble_gap_get_advertising_data(temp, sizeof(temp), nullptr);
                // Keep the previous AD Flags
                size_t len = advData->get(&temp[3], sizeof(temp) - 3);
                ret = ble_gap_set_advertising_data(temp, len + 3, nullptr);
            }
        }
        if (ret != SYSTEM_ERROR_NONE) {
            LOG_DEBUG(TRACE, "ble_gap_set_advertising_data failed: %d", ret);
        }
        return ret;
    }

    int setScanResponseData(const BleAdvertisingData* srData) {
        int ret;
        if (srData == nullptr) {
            ret = ble_gap_set_scan_response_data(nullptr, 0, nullptr);
            // TODO: keep the Particle vendor ID
        } else {
            BleAdvertisingData scanRespData = *srData;
            scanRespData.remove(BLE_SIG_AD_TYPE_FLAGS);
            ret = ble_gap_set_scan_response_data(scanRespData.data(), scanRespData.length(), nullptr);
        }
        if (ret != SYSTEM_ERROR_NONE) {
            LOG_DEBUG(TRACE, "ble_gap_set_scan_response_data failed: %d", ret);
        }
        return ret;
    }

    int advertise(void) {
        return ble_gap_start_advertising(nullptr);
    }

    int advertise(bool connectable) {
        int ret;
        if (connectable) {
            ret = setAdvertisingType(BLE_ADV_CONNECTABLE_SCANNABLE_UNDIRECRED_EVT);
        } else {
            ret = setAdvertisingType(BLE_ADV_SCANABLE_UNDIRECTED_EVT);
        }
        if (ret == SYSTEM_ERROR_NONE) {
            ret = advertise();
        }
        return ret;
    }

    int stopAdvertising(void) const {
        return ble_gap_stop_advertising();
    }

    void broadcasterProcessStopped(void) {
        return;
    }
};


/*******************************************************
 * BleObserverImpl definition
 */
class BleObserverImpl {
public:
    size_t targetCount;
    OnScanResultCallback callback;
    BleScanResult* results;
    size_t count;

    BleObserverImpl() : targetCount(0), callback(nullptr), results(nullptr), count(0) {}
    ~BleObserverImpl() {}

    int scan(OnScanResultCallback callback) {
        this->callback = callback;
        count = 0;
        ble_gap_start_scan(observerProcessScanResult, this, nullptr);
        return count;
    }

    int scan(OnScanResultCallback callback, uint16_t timeout) {
        hal_ble_scan_params_t params;
        ble_gap_get_scan_parameters(&params, nullptr);
        params.timeout = timeout;
        ble_gap_set_scan_parameters(&params, nullptr);
        return scan(callback);
    }

    int scan(BleScanResult* results, size_t resultCount) {
        this->results = results;
        targetCount = resultCount;
        count = 0;
        ble_gap_start_scan(observerProcessScanResult, this, nullptr);
        return count;
    }

    int scan(BleScanResult* results, size_t resultCount, uint16_t timeout) {
        hal_ble_scan_params_t params;
        ble_gap_get_scan_parameters(&params, nullptr);
        params.timeout = timeout;
        ble_gap_set_scan_parameters(&params, nullptr);
        return scan(results, resultCount);
    }

    int scan(BleScanResult* results, size_t resultCount, const BleScanParams& params) {
        ble_gap_set_scan_parameters(&params, nullptr);
        return scan(results, resultCount);
    }

    int stopScanning(void) {
        return ble_gap_stop_scan();
    }

    static void observerProcessScanResult(const hal_ble_gap_on_scan_result_evt_t* event, void* context) {
        BleObserverImpl* impl = static_cast<BleObserverImpl*>(context);
        BleScanResult result;
        result.address = event->peer_addr;
        result.rssi = event->rssi;
        result.scanResponse.set(event->sr_data, event->sr_data_len);
        result.advertisingData.set(event->adv_data, event->adv_data_len);

        if (impl->callback != nullptr) {
            impl->callback(&result);
        } else if (impl->results != nullptr) {
            if (impl->count < impl->targetCount) {
                impl->results[impl->count++] = result;
                if (impl->count >= impl->targetCount) {
                    impl->stopScanning();
                }
            }
        }
    }
};


/*******************************************************
 * BlePeripheralImpl definition
 */
class BlePeripheralImpl {
public:
    BleConnectionParams ppcp;
    Vector<BlePeerDevice> centrals;

    BlePeripheralImpl() {}
    ~BlePeripheralImpl() {}

    size_t centralCount(void) const {
        return centrals.size();
    }

    int setPPCP(uint16_t minInterval, uint16_t maxInterval, uint16_t latency, uint16_t timeout) {
        hal_ble_conn_params_t ppcp;
        ppcp.min_conn_interval = minInterval;
        ppcp.max_conn_interval = maxInterval;
        ppcp.slave_latency = latency;
        ppcp.conn_sup_timeout = timeout;
        return ble_gap_set_ppcp(&ppcp, nullptr);
    }

    int disconnect(void) {
        for (size_t i = 0; i < centralCount(); i++) {
            BlePeerDevice& central = centrals[i];
            ble_gap_disconnect(central.connHandle, nullptr);
            centrals.clear();
        }
        return SYSTEM_ERROR_NONE;
    }

    bool connected(void) const {
        return centrals.size() > 0;
    }

    void peripheralProcessConnected(const BlePeerDevice& peer) {
        if (centralCount() < BLE_MAX_PERIPHERAL_COUNT) {
            centrals.append(peer);
        }
    }

    void peripheralProcessDisconnected(const BlePeerDevice& peer) {
        centrals.clear();
    }
};


/*******************************************************
 * BleCentralImpl definition
 */
class BleCentralImpl {
public:
    Vector<BlePeerDevice> peripherals;

    BleCentralImpl() {}
    ~BleCentralImpl() {}

    size_t peripheralCount(void) const {
        return peripherals.size();
    }

    BlePeerDevice connect(const BleAddress& addr) {
        BlePeerDevice pseudo;
        int ret = ble_gap_connect(&addr, nullptr);
        if (ret != SYSTEM_ERROR_NONE) {
            LOG_DEBUG(TRACE, "ble_gap_set_scan_response_data failed: %d", ret);
            return pseudo;
        }
        for (int i = 0; i < peripherals.size(); i++) {
            const BlePeerDevice& peer = peripherals[i];
            if (peer.address == addr) {
                LOG(TRACE, "New peripheral connected. Start discovering services.");
                return peer;
            }
        }
        return pseudo;
    }

    int disconnect(const BlePeerDevice& peripheral) {
        for (size_t i = 0; i < peripheralCount(); i++) {
            BlePeerDevice& peer = peripherals[i];
            if (peer.connHandle == peripheral.connHandle) {
                ble_gap_disconnect(peer.connHandle, nullptr);
                peripherals.removeOne(peer);

                // clear CCCD
                return SYSTEM_ERROR_NONE;
            }
        }
        return SYSTEM_ERROR_NONE;
    }

    bool connected(void) const {
        return peripherals.size() > 0;
    }

    void centralProcessConnected(const BlePeerDevice& peer) {
        if (peripheralCount() < BLE_MAX_CENTRAL_COUNT) {
            peripherals.append(peer);
        }
    }

    void centralProcessDisconnected(const BlePeerDevice& peer) {
        peripherals.removeOne(peer);
    }
};


/*******************************************************
 * BlePeerDevice class
 */
BlePeerDevice::BlePeerDevice() {
    role = ROLE::INVALID;
    connHandle = BLE_INVALID_CONN_HANDLE;
    rssi = 0x7F;
    gattsProxy_ = std::make_shared<BleGattServerImpl>(address);
}

BlePeerDevice::BlePeerDevice(const BleAddress& addr) {
    role = ROLE::INVALID;
    connHandle = BLE_INVALID_CONN_HANDLE;
    rssi = 0x7F;
    gattsProxy_ = std::make_shared<BleGattServerImpl>(addr);
}

BlePeerDevice::~BlePeerDevice() {

}

BleCharacteristic BlePeerDevice::characteristic(const char* desc) const {
    return gattsProxy_->characteristic(desc);
}

BleCharacteristic BlePeerDevice::characteristic(const BleUuid& uuid) const {
    return gattsProxy_->characteristic(uuid);
}

bool BlePeerDevice::connected(void) const {
    return connHandle != BLE_INVALID_CONN_HANDLE;
}

bool BlePeerDevice::operator == (const BlePeerDevice& device) {
    if (connHandle == device.connHandle &&
        address == device.address) {
        return true;
    }
    return false;
}


/*******************************************************
 * BleLocalDevice class
 */
BleLocalDevice::BleLocalDevice()
    : connectedCb_(nullptr),
      disconnectedCb_(nullptr) {
    ble_stack_init(nullptr);
    ble_gap_get_device_address(&address);

    gattsProxy_ = std::make_unique<BleGattServerImpl>(address);
    gattcProxy_ = std::make_unique<BleGattClientImpl>();
    broadcasterProxy_ = std::make_unique<BleBroadcasterImpl>();
    observerProxy_ = std::make_unique<BleObserverImpl>();
    peripheralProxy_ = std::make_unique<BlePeripheralImpl>();
    centralProxy_ = std::make_unique<BleCentralImpl>();

    ble_set_callback_on_events(onBleEvents, this);
}

BleLocalDevice::~BleLocalDevice() {

}

BleLocalDevice& BleLocalDevice::getInstance(void) {
    static BleLocalDevice instance;
    return instance;
}

void BleLocalDevice::onConnected(OnConnectedCallback callback) {
    connectedCb_ = callback;
}

void BleLocalDevice::onDisconnected(OnDisconnectedCallback callback) {
    disconnectedCb_ = callback;
}

int BleLocalDevice::on(void) {
    return SYSTEM_ERROR_NONE;
}

void BleLocalDevice::off(void) {

}

int BleLocalDevice::setTxPower(int8_t val) const {
    return broadcasterProxy_->setTxPower(val);
}
int8_t BleLocalDevice::txPower(void) const {
    return broadcasterProxy_->txPower();
}

int BleLocalDevice::advertise(void) {
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(BleAdvertisingData* advertisingData, BleAdvertisingData* scanResponse) {
    broadcasterProxy_->setAdvertisingData(advertisingData);
    broadcasterProxy_->setScanResponseData(scanResponse);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(uint16_t interval) {
    broadcasterProxy_->setAdvertisingInterval(interval);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(uint16_t interval, BleAdvertisingData* advertisingData, BleAdvertisingData* scanResponse) {
    broadcasterProxy_->setAdvertisingData(advertisingData);
    broadcasterProxy_->setScanResponseData(scanResponse);
    broadcasterProxy_->setAdvertisingInterval(interval);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(uint16_t interval, uint16_t timeout) {
    broadcasterProxy_->setAdvertisingInterval(interval);
    broadcasterProxy_->setAdvertisingTimeout(timeout);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(uint16_t interval, uint16_t timeout, BleAdvertisingData* advertisingData, BleAdvertisingData* scanResponse) {
    broadcasterProxy_->setAdvertisingData(advertisingData);
    broadcasterProxy_->setScanResponseData(scanResponse);
    broadcasterProxy_->setAdvertisingInterval(interval);
    broadcasterProxy_->setAdvertisingTimeout(timeout);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(const BleAdvertisingParams& params) {
    broadcasterProxy_->setAdvertisingParams(&params);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(const BleAdvertisingParams& params, BleAdvertisingData* advertisingData, BleAdvertisingData* scanResponse) {
    broadcasterProxy_->setAdvertisingData(advertisingData);
    broadcasterProxy_->setScanResponseData(scanResponse);
    broadcasterProxy_->setAdvertisingParams(&params);
    return broadcasterProxy_->advertise();
}

int BleLocalDevice::advertise(const iBeacon& iBeacon, bool connectable) {
    broadcasterProxy_->setIbeacon(iBeacon);
    return broadcasterProxy_->advertise(connectable);
}

int BleLocalDevice::advertise(uint16_t interval, const iBeacon& iBeacon, bool connectable) {
    broadcasterProxy_->setIbeacon(iBeacon);
    broadcasterProxy_->setAdvertisingInterval(interval);
    return broadcasterProxy_->advertise(connectable);
}

int BleLocalDevice::advertise(uint16_t interval, uint16_t timeout, const iBeacon& iBeacon, bool connectable) {
    broadcasterProxy_->setIbeacon(iBeacon);
    broadcasterProxy_->setAdvertisingInterval(interval);
    broadcasterProxy_->setAdvertisingTimeout(timeout);
    return broadcasterProxy_->advertise(connectable);
}

int BleLocalDevice::advertise(const BleAdvertisingParams& params, const iBeacon& iBeacon, bool connectable) {
    broadcasterProxy_->setIbeacon(iBeacon);
    broadcasterProxy_->setAdvertisingParams(&params);
    return broadcasterProxy_->advertise(connectable);
}

int BleLocalDevice::stopAdvertising(void) const {
    return broadcasterProxy_->stopAdvertising();
}

int BleLocalDevice::scan(OnScanResultCallback callback) {
    return observerProxy_->scan(callback);
}

int BleLocalDevice::scan(OnScanResultCallback callback, uint16_t timeout) {
    return observerProxy_->scan(callback, timeout);
}

int BleLocalDevice::scan(BleScanResult* results, size_t resultCount) {
    return observerProxy_->scan(results, resultCount);
}

int BleLocalDevice::scan(BleScanResult* results, size_t resultCount, uint16_t timeout) {
    return observerProxy_->scan(results, resultCount, timeout);
}

int BleLocalDevice::scan(BleScanResult* results, size_t resultCount, const BleScanParams& params) {
    return observerProxy_->scan(results, resultCount, params);
}

int BleLocalDevice::stopScanning(void) {
    return observerProxy_->stopScanning();
}

int BleLocalDevice::setPPCP(uint16_t minInterval, uint16_t maxInterval, uint16_t latency, uint16_t timeout) {
    return peripheralProxy_->setPPCP(minInterval, maxInterval, latency, timeout);
}

int BleLocalDevice::disconnect(void) {
    return peripheralProxy_->disconnect();
}

bool BleLocalDevice::connected(void) const {
    return (peripheralProxy_->connected() || centralProxy_->connected());
}

BlePeerDevice BleLocalDevice::connect(const BleAddress& addr, uint16_t interval, uint16_t latency, uint16_t timeout) {
    hal_ble_conn_params_t connParams;
    connParams.min_conn_interval = interval;
    connParams.max_conn_interval = interval;
    connParams.slave_latency = latency;
    connParams.conn_sup_timeout = timeout;
    ble_gap_set_ppcp(&connParams, nullptr);
    return connect(addr);
}

BlePeerDevice BleLocalDevice::connect(const BleAddress& addr) {
    BlePeerDevice peer = centralProxy_->connect(addr);
    // Automatically discover services and characteristics.
    if (peer.connected()) {
        gattcProxy_->discoverAllServices(peer);
    }
    return peer;
}

int BleLocalDevice::disconnect(const BlePeerDevice& peripheral) {
    return centralProxy_->disconnect(peripheral);
}

int BleLocalDevice::addCharacteristic(BleCharacteristic& characteristic) {
    return gattsProxy_->addCharacteristic(characteristic);
}

int BleLocalDevice::addCharacteristic(const char* desc, BleCharacteristicProperties properties, OnDataReceivedCallback callback) {
    BleCharacteristic characteristic(desc, properties, callback);
    return gattsProxy_->addCharacteristic(characteristic);
}

BlePeerDevice* BleLocalDevice::findPeerDevice(BleConnectionHandle connHandle) {
    for (size_t i = 0; i < peripheralProxy_->centralCount(); i++) {
        BlePeerDevice* peer = &peripheralProxy_->centrals[i];
        if (peer != nullptr) {
            if (peer->connHandle == connHandle) {
                return peer;
            }
        }
    }
    for (size_t i = 0; i < centralProxy_->peripheralCount(); i++) {
        BlePeerDevice* peer = &centralProxy_->peripherals[i];
        if (peer != nullptr) {
            if (peer->connHandle == connHandle) {
                return peer;
            }
        }
    }
    return nullptr;
}

void BleLocalDevice::onBleEvents(const hal_ble_evts_t *event, void* context) {
    if (context == nullptr) {
        return;
    }

    auto bleInstance = static_cast<BleLocalDevice*>(context);

    switch (event->type) {
        case BLE_EVT_ADV_STOPPED: {
            bleInstance->broadcasterProxy_->broadcasterProcessStopped();
        } break;

        case BLE_EVT_CONNECTED: {
            BlePeerDevice peer;

            peer.connParams.conn_sup_timeout = event->params.connected.conn_sup_timeout;
            peer.connParams.slave_latency = event->params.connected.slave_latency;
            peer.connParams.max_conn_interval = event->params.connected.conn_interval;
            peer.connParams.min_conn_interval = event->params.connected.conn_interval;
            peer.connHandle = event->params.connected.conn_handle;
            peer.address = event->params.connected.peer_addr;

            if (bleInstance->connectedCb_) {
                bleInstance->connectedCb_(peer);
            }
            if (event->params.connected.role == BLE_ROLE_PERIPHERAL) {
                peer.role = ROLE::CENTRAL;
                bleInstance->peripheralProxy_->peripheralProcessConnected(peer);
            } else {
                peer.role = ROLE::PERIPHERAL;
                bleInstance->centralProxy_->centralProcessConnected(peer);
            }
        } break;

        case BLE_EVT_DISCONNECTED: {
            BlePeerDevice* peer = bleInstance->findPeerDevice(event->params.disconnected.conn_handle);
            if (peer != nullptr) {
                bleInstance->gattsProxy_->gattsProcessDisconnected(*peer);

                if (bleInstance->disconnectedCb_) {
                    bleInstance->disconnectedCb_(*peer);
                }
                if (peer->role & ROLE::PERIPHERAL) {
                    bleInstance->centralProxy_->centralProcessDisconnected(*peer);
                } else {
                    bleInstance->peripheralProxy_->peripheralProcessDisconnected(*peer);
                }
            }
        } break;

        case BLE_EVT_CONN_PARAMS_UPDATED: {

        } break;

        case BLE_EVT_DATA_WRITTEN: {
            LOG_DEBUG(TRACE, "onDataWritten, connection: %d, attribute: %d", event->params.data_rec.conn_handle, event->params.data_rec.attr_handle);

            BlePeerDevice* peer = bleInstance->findPeerDevice(event->params.data_rec.conn_handle);
            if (peer != nullptr) {
                bleInstance->gattsProxy_->gattsProcessDataWritten(event->params.data_rec.attr_handle,
                        event->params.data_rec.data, event->params.data_rec.data_len, *peer);
            }
        } break;

        case BLE_EVT_DATA_NOTIFIED: {
            LOG_DEBUG(TRACE, "onDataNotified, connection: %d, attribute: %d", event->params.data_rec.conn_handle, event->params.data_rec.attr_handle);

            BlePeerDevice* peer = bleInstance->findPeerDevice(event->params.data_rec.conn_handle);
            if (peer != nullptr) {
                bleInstance->gattcProxy_->gattcProcessDataNotified(event->params.data_rec.attr_handle,
                        event->params.data_rec.data, event->params.data_rec.data_len, *peer);
            }
        } break;

        default: break;
    }
}


BleLocalDevice& _fetch_ble() {
    return BleLocalDevice::getInstance();
}


#endif