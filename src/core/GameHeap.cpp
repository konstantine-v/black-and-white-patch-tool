#include "core/GameHeap.h"

namespace bw {

char* GameAllocateString(std::size_t size) noexcept
{
    auto* text = static_cast<char*>(GameAllocate(size));
    if (text != nullptr && size != 0) {
        text[0] = '\0';
    }
    return text;
}

}  // namespace bw
