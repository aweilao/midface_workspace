#pragma once

#include "core/MidSurfaceNewTypes.hpp"
#include "logical.h"

#include <stdio.h>
#include <string>

namespace midsurface_new
{
class StructuredEventWriter
{
public:
    StructuredEventWriter();
    ~StructuredEventWriter();

    logical Open(const char* file_path, logical append);
    void Close();
    logical IsOpen() const;
    logical Write(const StructuredEvent& event);

private:
    StructuredEventWriter(const StructuredEventWriter&);
    StructuredEventWriter& operator=(const StructuredEventWriter&);

private:
    FILE* fp_;
};
} // namespace midsurface_new
