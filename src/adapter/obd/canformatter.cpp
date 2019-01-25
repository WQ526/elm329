/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2017 ObdDiag.Net. All rights reserved.
 *
 */

#include <memory>
#include <cstdio>
#include "canmsgbuffer.h"
#include "isocan.h"

using namespace std;
using namespace util;

const uint32_t FMT_STD  = 0;
const uint32_t FMT_H1   = 1;
const uint32_t FMT_CAF0 = 2;
    

CanReplyFormatter::CanReplyFormatter()
{
    config_ = AdapterConfig::instance();
}

uint32_t CanReplyFormatter::getConfigKey()
{
    uint32_t key = FMT_STD;
    if (config_->getBoolProperty(PAR_HEADER_SHOW))
        key = FMT_H1;
    if (!config_->getBoolProperty(PAR_CAN_CAF)) 
        key = FMT_CAF0;
    
    return key;
}

/**
 * Process single frame
 * @param[in] msg CanMsgbuffer instance pointer
 */
void CanReplyFormatter::reply(const CanMsgBuffer* msg) 
{
    util::string str;
    bool canExtAddr = config_->getBytesProperty(PAR_CAN_EXT)->length;
    uint32_t offst = canExtAddr ? 2 : 1;
    uint32_t dlen = msg->data[offst - 1];
    
    switch (getConfigKey()) {
        case FMT_STD:
            replyH0(msg, offst, dlen, str);
            break;    
        case FMT_H1:
            replyH1(msg, (dlen + offst), str); // include PCI (and extended) bytes
            break;
        case FMT_CAF0:
            replyCAF0(msg, str);
            break;
    }
    
    AdptSendReply(str);
}

/**
 * Process first frame
 * @param[in] msg CanMsgbuffer instance pointer
 */
void CanReplyFormatter::replyFirstFrame(const CanMsgBuffer* msg) 
{
    util::string str;
    bool canExtAddr = config_->getBytesProperty(PAR_CAN_EXT)->length;
    uint32_t offst = canExtAddr ? 2 : 1;
    uint32_t dlen = canExtAddr ? 5 : 6;

    switch (getConfigKey()) {
        case FMT_STD:
            replyFirstFrameH0(msg, offst, dlen, str);
            break;    
        case FMT_H1:
            replyH1(msg, 8, str);
            break;
        case FMT_CAF0:
            replyCAF0(msg, str);
            break;
    }
    
    AdptSendReply(str);
}

/**
 * Process next frame
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[in] num Frame sequential number
 */
void CanReplyFormatter::replyNextFrame(const CanMsgBuffer* msg, int num) 
{
    util::string str;
    bool canExtAddr = config_->getBytesProperty(PAR_CAN_EXT)->length;
    uint32_t offst = canExtAddr ? 2 : 1;
    uint32_t dlen = canExtAddr ? 6 : 7;
 
    switch (getConfigKey()) {
        case FMT_STD:
            replyNextFrameH0(msg, offst, dlen,  num, str);
            break;    
        case FMT_H1:
            replyH1(msg, 8, str);
            break;
        case FMT_CAF0:
            replyCAF0(msg, str);
            break;
    }
    
    AdptSendReply(str);
}

/**
 * Format reply for "H0" option
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[in] dlen The number of bytes in the response
 * @param[out] str The output string
 */
void CanReplyFormatter::replyH0(const CanMsgBuffer* msg, uint32_t offst, uint32_t dlen, util::string& str)
{
    to_ascii(msg->data + offst, dlen, str); // skip PCI (and ext) bytes
}

/**
 * Format reply for "H1" option
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[in] dlen The number of bytes in the response
 * @param[out] str The output string
 */
void CanReplyFormatter::replyH1(const CanMsgBuffer* msg, uint32_t dlen, util::string& str)
{
    CanIDToString(msg->id, str, msg->extended);
    bool useSpaces = config_->getBoolProperty(PAR_SPACES);
    bool isDLC = config_->getBoolProperty(PAR_CAN_DLC);
    
    if (useSpaces) {
        str += ' '; // number of chars + space
    }
    if (isDLC) {
        str += msg->dlc + '0'; // add DLC byte
        if (useSpaces) {
            str += ' ';
        }
    }
    to_ascii(msg->data, dlen, str);
}

/**
 * Format reply for "CAF0" option
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[in] dlen The number of bytes in the response
 * @param[out] str The output string
 */
void CanReplyFormatter::replyCAF0(const CanMsgBuffer* msg, util::string& str)
{
    if (config_->getBoolProperty(PAR_HEADER_SHOW)) {
        CanIDToString(msg->id, str, msg->extended);
        if (config_->getBoolProperty(PAR_SPACES)) {
            str += ' '; // number of chars + space
        }
    }
    to_ascii(msg->data, 8, str);
}

/**
 * Process first frame with H0 option
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[out] str The output string
 */
void CanReplyFormatter::replyFirstFrameH0(const CanMsgBuffer* msg, uint32_t offst, uint32_t dlen, util::string& str)
{
    uint32_t msgLen = (msg->data[offst - 1] & 0x0F) << 8 | msg->data[offst];

    CanIDToString(msgLen, str, false); // we need only 3 digits
    AdptSendReply(str);
    str = "0: ";
    to_ascii((msg->data + offst + 1), dlen, str);
}

/**
 * Process next frame with H0 option
 * @param[in] msg CanMsgbuffer instance pointer
 * @param[in] num Frame sequential number
 * @param[out] str The output string
 */
void CanReplyFormatter::replyNextFrameH0(const CanMsgBuffer* msg, uint32_t offst, uint32_t dlen, int num, util::string& str)
{
    char prefix[4]; // space for 1.5 bytes max
    sprintf(prefix, "%X: ", (num & 0x0F));
    str = prefix;
    to_ascii((msg->data + offst), dlen, str);
}
