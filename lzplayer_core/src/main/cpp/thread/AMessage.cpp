/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_TAG "AMessage"
//#define LOG_NDEBUG 0
//#define DUMP_STATS

#include <ctype.h>

#include "AMessage.h"

#include "ADebug.h"
#include "ALooperRoster.h"
#include "AHandler.h"

// #include <media/stagefright/foundation/hexdump.h>

// #if defined(__ANDROID__) && !defined(__ANDROID_VNDK__) && !defined(__ANDROID_APEX__)
// #include <binder/Parcel.h>
// #endif

extern ALooperRoster gLooperRoster;

status_t AReplyToken::setReply(const std::shared_ptr<AMessage> &reply) {
    if (mReplied) {
        ALOGE("trying to post a duplicate reply");
        return -EBUSY;
    }
    assert(mReply == NULL);
    mReply = reply;
    mReplied = true;
    return OK;
}

AMessage::AMessage(void)
    : mWhat(0),
      mTarget(0) {
}

AMessage::AMessage(uint32_t what, const std::shared_ptr<AHandler> &handler)
    : mWhat(what) {
    setTarget(handler);
}

AMessage::~AMessage() {
    clear();
}

void AMessage::setWhat(uint32_t what) {
    mWhat = what;
}

uint32_t AMessage::what() const {
    return mWhat;
}

void AMessage::setTarget(const std::shared_ptr<AHandler> &handler) {
    if (handler == NULL) {
        mTarget = 0;
        mHandler.reset();
        mLooper.reset();
    } else {
        mTarget = handler->id();
        mHandler = handler->getHandler();
        mLooper = handler->getLooper();
    }
}

void AMessage::clear() {
    // Item needs to be handled delicately
    for (Item &item : mItems) {
        delete[] item.mName;
        item.mName = NULL;
        freeItemValue(&item);
    }
    mItems.clear();
}

void AMessage::freeItemValue(Item *item) {
    switch (item->mType) {
        case kTypeString:
        {
            free(item->u.stringValue);
            item->u.stringValue = nullptr;
            break;
        }
        default:
            break;
    }
    item->mType = kTypeInt32; // clear type
}

#ifdef DUMP_STATS
#include <utils/Mutex.h>

Mutex gLock;
static int32_t gFindItemCalls = 1;
static int32_t gDupCalls = 1;
static int32_t gAverageNumItems = 0;
static int32_t gAverageNumChecks = 0;
static int32_t gAverageNumMemChecks = 0;
static int32_t gAverageDupItems = 0;
static int32_t gLastChecked = -1;

static void reportStats() {
    int32_t time = (ALooper::GetNowUs() / 1000);
    if (time / 1000 != gLastChecked / 1000) {
        gLastChecked = time;
        ALOGI("called findItemIx %zu times (for len=%.1f i=%.1f/%.1f mem) dup %zu times (for len=%.1f)",
                gFindItemCalls,
                gAverageNumItems / (float)gFindItemCalls,
                gAverageNumChecks / (float)gFindItemCalls,
                gAverageNumMemChecks / (float)gFindItemCalls,
                gDupCalls,
                gAverageDupItems / (float)gDupCalls);
        gFindItemCalls = gDupCalls = 1;
        gAverageNumItems = gAverageNumChecks = gAverageNumMemChecks = gAverageDupItems = 0;
        gLastChecked = time;
    }
}
#endif

inline size_t AMessage::findItemIndex(const char *name, size_t len) const {
#ifdef DUMP_STATS
    size_t memchecks = 0;
#endif
    size_t i = 0;
    for (; i < mItems.size(); i++) {
        if (len != mItems[i].mNameLength) {
            continue;
        }
#ifdef DUMP_STATS
        ++memchecks;
#endif
        if (!memcmp(mItems[i].mName, name, len)) {
            break;
        }
    }
#ifdef DUMP_STATS
    {
        Mutex::Autolock _l(gLock);
        ++gFindItemCalls;
        gAverageNumItems += mItems.size();
        gAverageNumMemChecks += memchecks;
        gAverageNumChecks += i;
        reportStats();
    }
#endif
    return i;
}

// assumes item's name was uninitialized or NULL
void AMessage::Item::setName(const char *name, size_t len) {
    mNameLength = len;
    mName = new char[len + 1];
    memcpy((void*)mName, name, len + 1);
}

AMessage::Item::Item(const char *name, size_t len)
    : mType(kTypeInt32) {
    // mName and mNameLength are initialized by setName
    setName(name, len);
}

AMessage::Item *AMessage::allocateItem(const char *name) {
    size_t len = strlen(name);
    size_t i = findItemIndex(name, len);
    Item *item;

    if (i < mItems.size()) {
        item = &mItems[i];
        freeItemValue(item);
    } else {
        CHECK(mItems.size() < kMaxNumItems);
        i = mItems.size();
        // place a 'blank' item at the end - this is of type kTypeInt32
        mItems.emplace_back(name, len);
        item = &mItems[i];
    }

    return item;
}

const AMessage::Item *AMessage::findItem(
        const char *name, Type type) const {
    size_t i = findItemIndex(name, strlen(name));
    if (i < mItems.size()) {
        const Item *item = &mItems[i];
        return item->mType == type ? item : NULL;

    }
    return NULL;
}

bool AMessage::findAsFloat(const char *name, float *value) const {
    size_t i = findItemIndex(name, strlen(name));
    if (i < mItems.size()) {
        const Item *item = &mItems[i];
        switch (item->mType) {
            case kTypeFloat:
                *value = item->u.floatValue;
                return true;
            case kTypeDouble:
                *value = (float)item->u.doubleValue;
                return true;
            case kTypeInt64:
                *value = (float)item->u.int64Value;
                return true;
            case kTypeInt32:
                *value = (float)item->u.int32Value;
                return true;
            case kTypeSize:
                *value = (float)item->u.sizeValue;
                return true;
            default:
                return false;
        }
    }
    return false;
}

bool AMessage::findAsInt64(const char *name, int64_t *value) const {
    size_t i = findItemIndex(name, strlen(name));
    if (i < mItems.size()) {
        const Item *item = &mItems[i];
        switch (item->mType) {
            case kTypeInt64:
                *value = item->u.int64Value;
                return true;
            case kTypeInt32:
                *value = item->u.int32Value;
                return true;
            default:
                return false;
        }
    }
    return false;
}

bool AMessage::contains(const char *name) const {
    size_t i = findItemIndex(name, strlen(name));
    return i < mItems.size();
}

#define BASIC_TYPE(NAME,FIELDNAME,TYPENAME)                             \
void AMessage::set##NAME(const char *name, TYPENAME value) {            \
    Item *item = allocateItem(name);                                    \
    if (item) {                                                         \
        item->mType = kType##NAME;                                      \
        item->u.FIELDNAME = value;                                      \
    }                                                                   \
}                                                                       \
                                                                        \
/* NOLINT added to avoid incorrect warning/fix from clang.tidy */       \
bool AMessage::find##NAME(const char *name, TYPENAME *value) const {  /* NOLINT */ \
    const Item *item = findItem(name, kType##NAME);                     \
    if (item) {                                                         \
        *value = item->u.FIELDNAME;                                     \
        return true;                                                    \
    }                                                                   \
    return false;                                                       \
}

BASIC_TYPE(Int32,int32Value,int32_t)
BASIC_TYPE(Int64,int64Value,int64_t)
BASIC_TYPE(Size,sizeValue,size_t)
BASIC_TYPE(Float,floatValue,float)
BASIC_TYPE(Double,doubleValue,double)
BASIC_TYPE(Pointer,ptrValue,void *)

#undef BASIC_TYPE

void AMessage::setString(
        const char *name, const char *s, ssize_t len) {
    Item *item = allocateItem(name);
    if (item) {
        item->mType = kTypeString;
        int size = len < 0 ? strlen(s) : len;
        item->u.stringValue = (char*)malloc(size);
        item->strLen = size;
        memcpy(item->u.stringValue,s,size);
    }
}

void AMessage::setString(
        const char *name, const std::string &s) {
    setString(name, s.c_str(), s.size());
}

void AMessage::setObjectInternal(
        const char *name, const std::shared_ptr<void> &obj, Type type) {
    Item *item = allocateItem(name);
    if (item) {
        item->mType = type;
        item->shObj = std::static_pointer_cast<void>(obj);
    }
}

void AMessage::setObject(const char *name, const std::shared_ptr<void> &obj) {
    setObjectInternal(name, obj, kTypeObject);
}

void AMessage::setRect(
        const char *name,
        int32_t left, int32_t top, int32_t right, int32_t bottom) {
    Item *item = allocateItem(name);
    if (item) {
        item->mType = kTypeRect;

        item->u.rectValue.mLeft = left;
        item->u.rectValue.mTop = top;
        item->u.rectValue.mRight = right;
        item->u.rectValue.mBottom = bottom;
    }
}

bool AMessage::findString(const char *name, std::string& value) const {
    const Item *item = findItem(name, kTypeString);
    if (item) {
        value = std::string(item->u.stringValue,item->strLen);
        return true;
    }
    return false;
}

bool AMessage::findObject(const char *name, std::shared_ptr<void> *obj) const {
    const Item *item = findItem(name, kTypeObject);
    if (item) {
        *obj = item->shObj;
        return true;
    }
    return false;
}

bool AMessage::findRect(
        const char *name,
        int32_t *left, int32_t *top, int32_t *right, int32_t *bottom) const {
    const Item *item = findItem(name, kTypeRect);
    if (item == NULL) {
        return false;
    }

    *left = item->u.rectValue.mLeft;
    *top = item->u.rectValue.mTop;
    *right = item->u.rectValue.mRight;
    *bottom = item->u.rectValue.mBottom;

    return true;
}

void AMessage::deliver() {
    std::shared_ptr<AHandler> handler = mHandler.lock();
    if (handler == NULL) {
        ALOGW("failed to deliver message as target handler %d is gone.", mTarget);
        return;
    }

    handler->deliverMessage(shared_from_this());
}

status_t AMessage::post(int64_t delayUs) {
    std::shared_ptr<ALooper> looper = mLooper.lock();
    if (looper == NULL) {
        ALOGW("failed to post message as target looper for handler %d is gone.", mTarget);
        return -ENOENT;
    }

    looper->post(shared_from_this(), delayUs);
    return OK;
}

status_t AMessage::postAndAwaitResponse(std::shared_ptr<AMessage> *response) {
    std::shared_ptr<ALooper> looper = mLooper.lock();
    if (looper == NULL) {
        ALOGW("failed to post message as target looper for handler %d is gone.", mTarget);
        return -ENOENT;
    }

    std::shared_ptr<AReplyToken> token = looper->createReplyToken();
    if (token == NULL) {
        ALOGE("failed to create reply token");
        return -ENOMEM;
    }
    setObject("replyID", token);

    looper->post(shared_from_this(), 0 /* delayUs */);
    return looper->awaitResponse(token, response);
}

status_t AMessage::postReply(const std::shared_ptr<AReplyToken> &replyToken) {
    if (replyToken == NULL) {
        ALOGW("failed to post reply to a NULL token");
        return -ENOENT;
    }
    std::shared_ptr<ALooper> looper = replyToken->getLooper();
    if (looper == NULL) {
        ALOGW("failed to post reply as target looper is gone.");
        return -ENOENT;
    }
    return looper->postReply(replyToken, shared_from_this());
}

bool AMessage::senderAwaitsResponse(std::shared_ptr<AReplyToken> &replyToken) {
    std::shared_ptr<void> tmp;
    bool found = findObject("replyID", &tmp);
    if (!found) {
        return false;
    }

    std::shared_ptr<AReplyToken> tmpToken = std::static_pointer_cast<AReplyToken>(tmp);
    // *replyToken = tmpToken;
    replyToken = tmpToken;
    tmp.reset();
    setObject("replyID", tmp);
    // TODO: delete Object instead of setting it to NULL

    return replyToken != NULL;
}

std::shared_ptr<AMessage> AMessage::dup() const {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(mWhat, mHandler.lock());
    msg->mItems = mItems;

#ifdef DUMP_STATS
    {
        Mutex::Autolock _l(gLock);
        ++gDupCalls;
        gAverageDupItems += mItems.size();
        reportStats();
    }
#endif

    for (size_t i = 0; i < mItems.size(); ++i) {
        const Item *from = &mItems[i];
        Item *to = &msg->mItems[i];

        to->setName(from->mName, from->mNameLength);
        to->mType = from->mType;

        switch (from->mType) {
            case kTypeString:
            {
                to->u.stringValue = (char*)malloc(from->strLen);
                memcpy(to->u.stringValue,from->u.stringValue,from->strLen);
                to->strLen = from->strLen;
                break;
            }
            default:
            {
                to->u = from->u;
                break;
            }
        }
    }

    return msg;
}

static void appendIndent(std::string *s, int32_t indent) {
    static const char kWhitespace[] =
        "                                        "
        "                                        ";

    // CHECK_LT((size_t)indent, sizeof(kWhitespace));

    s->append(kWhitespace, indent);
}

static bool isFourcc(uint32_t what) {
    return isprint(what & 0xff)
        && isprint((what >> 8) & 0xff)
        && isprint((what >> 16) & 0xff)
        && isprint((what >> 24) & 0xff);
}

std::string AMessage::debugString(int32_t indent) const {

    std::string s = "AMessage(what = ";

    // std::string tmp;
    // if (isFourcc(mWhat)) {
    //     tmp = AStringPrintf(
    //             "'%c%c%c%c'",
    //             (char)(mWhat >> 24),
    //             (char)((mWhat >> 16) & 0xff),
    //             (char)((mWhat >> 8) & 0xff),
    //             (char)(mWhat & 0xff));
    // } else {
    //     tmp = AStringPrintf("0x%08x", mWhat);
    // }
    // s.append(tmp);

    // if (mTarget != 0) {
    //     tmp = AStringPrintf(", target = %d", mTarget);
    //     s.append(tmp);
    // }
    // s.append(") = {\n");

    // for (size_t i = 0; i < mItems.size(); ++i) {
    //     const Item &item = mItems[i];

    //     switch (item.mType) {
    //         case kTypeInt32:
    //             tmp = AStringPrintf(
    //                     "int32_t %s = %d", item.mName, item.u.int32Value);
    //             break;
    //         case kTypeInt64:
    //             tmp = AStringPrintf(
    //                     "int64_t %s = %lld", item.mName, item.u.int64Value);
    //             break;
    //         case kTypeSize:
    //             tmp = AStringPrintf(
    //                     "size_t %s = %d", item.mName, item.u.sizeValue);
    //             break;
    //         case kTypeFloat:
    //             tmp = AStringPrintf(
    //                     "float %s = %f", item.mName, item.u.floatValue);
    //             break;
    //         case kTypeDouble:
    //             tmp = AStringPrintf(
    //                     "double %s = %f", item.mName, item.u.doubleValue);
    //             break;
    //         case kTypePointer:
    //             tmp = AStringPrintf(
    //                     "void *%s = %p", item.mName, item.u.ptrValue);
    //             break;
    //         case kTypeString:
    //             tmp = AStringPrintf(
    //                     "string %s = \"%s\"",
    //                     item.mName,
    //                     item.u.stringValue->c_str());
    //             break;
    //         case kTypeObject:
    //             tmp = AStringPrintf(
    //                     "ABase *%s = %p", item.mName, item.u.refValue);
    //             break;
    //         case kTypeBuffer:
    //         {
    //             std::shared_ptr<ABuffer> buffer = static_cast<ABuffer *>(item.u.refValue);

    //             if (buffer != NULL && buffer->data() != NULL && buffer->size() <= 64) {
    //                 tmp = AStringPrintf("Buffer %s = {\n", item.mName);
    //                 hexdump(buffer->data(), buffer->size(), indent + 4, &tmp);
    //                 appendIndent(&tmp, indent + 2);
    //                 tmp.append("}");
    //             } else {
    //                 tmp = AStringPrintf(
    //                         "Buffer *%s = %p", item.mName, buffer.get());
    //             }
    //             break;
    //         }
    //         case kTypeMessage:
    //             tmp = AStringPrintf(
    //                     "AMessage %s = %s",
    //                     item.mName,
    //                     static_cast<AMessage *>(
    //                         item.u.refValue)->debugString(
    //                             indent + strlen(item.mName) + 14).c_str());
    //             break;
    //         case kTypeRect:
    //             tmp = AStringPrintf(
    //                     "Rect %s(%d, %d, %d, %d)",
    //                     item.mName,
    //                     item.u.rectValue.mLeft,
    //                     item.u.rectValue.mTop,
    //                     item.u.rectValue.mRight,
    //                     item.u.rectValue.mBottom);
    //             break;
    //         default:
    //             TRESPASS();
    //     }

    //     appendIndent(&s, indent);
    //     s.append("  ");
    //     s.append(tmp);
    //     s.append("\n");
    // }

    // appendIndent(&s, indent);
    // s.append("}");

    return s;
}

size_t AMessage::countEntries() const {
    return mItems.size();
}

const char *AMessage::getEntryNameAt(size_t index, Type *type) const {
    if (index >= mItems.size()) {
        *type = kTypeInt32;

        return NULL;
    }

    *type = mItems[index].mType;

    return mItems[index].mName;
}

status_t AMessage::setEntryNameAt(size_t index, const char *name) {
    if (index >= mItems.size()) {
        return BAD_INDEX;
    }
    if (name == nullptr) {
        return BAD_VALUE;
    }
    if (!strcmp(name, mItems[index].mName)) {
        return OK; // name has not changed
    }
    size_t len = strlen(name);
    if (findItemIndex(name, len) < mItems.size()) {
        return ALREADY_EXISTS;
    }
    delete[] mItems[index].mName;
    mItems[index].mName = nullptr;
    mItems[index].setName(name, len);
    return OK;
}

status_t AMessage::removeEntryAt(size_t index) {
    if (index >= mItems.size()) {
        return BAD_INDEX;
    }
    // delete entry data and objects
    delete[] mItems[index].mName;
    mItems[index].mName = nullptr;
    freeItemValue(&mItems[index]);

    // swap entry with last entry and clear last entry's data
    size_t lastIndex = mItems.size() - 1;
    if (index < lastIndex) {
        mItems[index] = mItems[lastIndex];
        mItems[lastIndex].mName = nullptr;
        mItems[lastIndex].mType = kTypeInt32;
    }
    mItems.pop_back();
    return OK;
}

status_t AMessage::removeEntryByName(const char *name) {
    if (name == nullptr) {
        return BAD_VALUE;
    }
    size_t index = findEntryByName(name);
    if (index >= mItems.size()) {
        return BAD_INDEX;
    }
    return removeEntryAt(index);
}

void AMessage::extend(const std::shared_ptr<AMessage> &other) {
    // ignore null messages
    if (other == nullptr) {
        return;
    }

    for (size_t ix = 0; ix < other->mItems.size(); ++ix) {
        allocateItem(other->mItems[ix].mName);
    }
}

size_t AMessage::findEntryByName(const char *name) const {
    return name == nullptr ? countEntries() : findItemIndex(name, strlen(name));
}
