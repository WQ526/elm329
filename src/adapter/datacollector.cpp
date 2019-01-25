/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2017 ObdDiag.Net. All rights reserved.
 *
 */

#include <climits>
#include <cctype>
#include <memory>
#include <cstdlib>
#include <adaptertypes.h>
#include "datacollector.h"

using namespace std;
using namespace util;

const int STR_LEN = 14;
const int DAT_LEN = OBD_IN_MSG_DLEN;

DataCollector::DataCollector() 
  : str_(STR_LEN), length_(0), previous_(0), binary_(true)
{
    data_ = new uint8_t[DAT_LEN];
}

void DataCollector::putChar(char ch)
{
    if (ch == ' ' || ch == 0 ) // Ignore spaces
        return;
    
    // Make it uppercase
    ch = toupper(ch);
    binary_ = binary_ ? isxdigit(ch) : false;
    
    if (str_.length() < STR_LEN) {
        str_ += ch;
    }
    if (length_ < DAT_LEN) {
        if (binary_ && previous_) {
            char twoChars[] = { previous_, ch, 0 }; 
            data_[length_++] = strtoul(twoChars, 0, 16);
            previous_ = 0;
        }
        else {
            previous_ = ch; // save for the next ops
        }
    }
}

DataCollector* DataCollector::instance()
{
    static DataCollector instance;
    return &instance;
}

void DataCollector::reset()
{
    str_.clear();
    length_   = 0;
    previous_ = 0;
    binary_   = true;
}
