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

#ifndef A_MESSAGE_H_

#define A_MESSAGE_H_

#include "ALooper.h"
#include "ADebug.h"
#include <vector>
#include <memory>
#include <iostream>

struct AHandler;

struct AReplyToken{
    explicit AReplyToken(const std::shared_ptr<ALooper> &looper)
        : mLooper(looper),
          mReplied(false) {
    }

private:
    friend struct AMessage;
    friend struct ALooper;
    std::weak_ptr<ALooper> mLooper;
    std::shared_ptr<AMessage> mReply;
    bool mReplied;

    std::shared_ptr<ALooper> getLooper() const {
        return mLooper.lock();
    }
    // if reply is not set, returns false; otherwise, it retrieves the reply and returns true
    bool retrieveReply(std::shared_ptr<AMessage> *reply) {
        if (mReplied) {
            *reply = mReply;
            mReply.reset();
        }
        return mReplied;
    }
    // sets the reply for this token. returns OK or error
    status_t setReply(const std::shared_ptr<AMessage> &reply);
};

struct AMessage :public std::enable_shared_from_this<AMessage> {
    AMessage();
    AMessage(uint32_t what, const std::shared_ptr<AHandler> &handler);

    void setWhat(uint32_t what);
    uint32_t what() const;

    void setTarget(const std::shared_ptr<AHandler> &handler);

    // removes all items
    void clear();

    void setInt32(const char *name, int32_t value);
    void setInt64(const char *name, int64_t value);
    void setSize(const char *name, size_t value);
    void setFloat(const char *name, float value);
    void setDouble(const char *name, double value);
    void setPointer(const char *name, void *value);
    void setString(const char *name, const char *s, ssize_t len = -1);
    void setString(const char *name, const std::string   &s);
    void setObject(const char *name, const std::shared_ptr<void> &obj);

    void setRect(
            const char *name,
            int32_t left, int32_t top, int32_t right, int32_t bottom);

    bool contains(const char *name) const;

    bool findInt32(const char *name, int32_t *value) const;
    bool findInt64(const char *name, int64_t *value) const;
    bool findSize(const char *name, size_t *value) const;
    bool findFloat(const char *name, float *value) const;
    bool findDouble(const char *name, double *value) const;
    bool findPointer(const char *name, void **value) const;
    bool findString(const char *name, std::string& value) const;
    bool findObject(const char *name, std::shared_ptr<void> *obj) const;

    // finds signed integer types cast to int64_t
    bool findAsInt64(const char *name, int64_t *value) const;

    // finds any numeric type cast to a float
    bool findAsFloat(const char *name, float *value) const;

    bool findRect(
            const char *name,
            int32_t *left, int32_t *top, int32_t *right, int32_t *bottom) const;

    status_t post(int64_t delayUs = 0);

    // Posts the message to its target and waits for a response (or error)
    // before returning.
    status_t postAndAwaitResponse(std::shared_ptr<AMessage> *response);

    // If this returns true, the sender of this message is synchronously
    // awaiting a response and the reply token is consumed from the message
    // and stored into replyID. The reply token must be used to send the response
    // using "postReply" below.
    bool senderAwaitsResponse(std::shared_ptr<AReplyToken> &replyID);

    // Posts the message as a response to a reply token.  A reply token can
    // only be used once. Returns OK if the response could be posted; otherwise,
    // an error.
    status_t postReply(const std::shared_ptr<AReplyToken> &replyID);

    // Performs a deep-copy of "this", contained messages are in turn "dup'ed".
    // Warning: RefBase items, i.e. "objects" are _not_ copied but only have
    // their refcount incremented.
    std::shared_ptr<AMessage> dup() const;

    // Adds all items from other into this.
    void extend(const std::shared_ptr<AMessage> &other);

    std::string   debugString(int32_t indent = 0) const;

    enum Type {
        kTypeInt32,
        kTypeInt64,
        kTypeSize,
        kTypeFloat,
        kTypeDouble,
        kTypePointer,
        kTypeString,
        kTypeRect,
        kTypeObject
    };

    void setObjectInternal(const char *name, const std::shared_ptr<void> &obj, Type type);

    struct Rect {
        int32_t mLeft, mTop, mRight, mBottom;
    };

    size_t countEntries() const;
    const char *getEntryNameAt(size_t index, Type *type) const;

    /**
     * Removes an entry based on index.
     *
     * \param index index of the entry
     *
     * \retval OK the entry was removed successfully
     * \retval BAD_INDEX invalid index
     */
    status_t removeEntryAt(size_t index);

    /**
     * Removes an entry based on name.
     *
     * \param name  name of the entry
     *
     * \retval OK the entry was removed successfully
     * \retval BAD_VALUE name is invalid (null)
     * \retval BAD_INDEX name not found
     */
    status_t removeEntryByName(const char *name);

    /**
     * Sets the name of an entry based on index.
     *
     * \param index index of the entry
     * \param name (new) name of the entry
     *
     * \retval OK the name was set successfully
     * \retval BAD_INDEX invalid index
     * \retval BAD_VALUE name is invalid (null)
     * \retval ALREADY_EXISTS name is already used by another entry
     */
    status_t setEntryNameAt(size_t index, const char *name);

    /**
     * Finds an entry by name and returns its index.
     *
     * \retval countEntries() if the entry is not found.
     */
    size_t findEntryByName(const char *name) const;

    virtual ~AMessage();

private:
    friend struct ALooper; // deliver()

    uint32_t mWhat;

    // used only for debugging
    ALooper::handler_id mTarget;

    std::weak_ptr<AHandler> mHandler;
    std::weak_ptr<ALooper> mLooper;

    struct Item {
        union {
            int32_t int32Value;
            int64_t int64Value;
            size_t sizeValue;
            float floatValue;
            double doubleValue;
            char* stringValue;
            void *ptrValue;
            Rect rectValue;
        } u;
        std::shared_ptr<void> shObj;
        const char *mName;
        size_t      mNameLength;
        Type mType;
        size_t strLen = 0;
        void setName(const char *name, size_t len);
        Item() : mName(nullptr), mNameLength(0), mType(kTypeInt32) {
         }
        Item(const char *name, size_t length);
    };

    enum {
        kMaxNumItems = 256
    };
    std::vector<Item> mItems;

    /**
     * Allocates an item with the given key |name|. If the key already exists, the corresponding
     * item value is freed. Otherwise a new item is added.
     *
     * This method currently asserts if the number of elements would exceed the max number of
     * elements allowed (kMaxNumItems). This is a security precaution to avoid arbitrarily large
     * AMessage structures.
     *
     * @todo(b/192153245) Either revisit this security precaution, or change the behavior to
     *      silently ignore keys added after the max number of elements are reached.
     *
     * @note All previously returned Item* pointers are deemed invalid after this call. (E.g. from
     *       allocateItem or findItem)
     *
     * @param name the key for the requested item.
     *
     * @return Item* a pointer to the item.
     */
    Item *allocateItem(const char *name);

    /** Frees the value for the item. */
    void freeItemValue(Item *item);

    /** Finds an item with given key |name| and |type|. Returns nullptr if item is not found. */
    const Item *findItem(const char *name, Type type) const;

    size_t findItemIndex(const char *name, size_t len) const;

    void deliver();

    DISALLOW_EVIL_CONSTRUCTORS(AMessage);
};

#endif  // A_MESSAGE_H_
