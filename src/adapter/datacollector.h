/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2017 ObdDiag.Net. All rights reserved.
 *
 */

#ifndef __DATACOLLECTOR_H__ 
#define __DATACOLLECTOR_H__

#include <lstring.h>

class DataCollector {
public:
    const util::string& getString() const {return str_; }
    const uint8_t* getData() const { return data_; }
    uint16_t getLength() const { return length_; }
    bool isData() const { return binary_; }
    void putChar(char ch);
    void reset();
    static DataCollector* instance();
private:
    DataCollector();
    util::string str_;
    uint8_t* data_;
    uint16_t length_;
    char previous_;
    bool binary_;
};

#endif //__DATACOLLECTOR_H__
