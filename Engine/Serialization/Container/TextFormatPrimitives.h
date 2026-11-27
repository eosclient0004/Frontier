/*============================================================================================================================================
                                                           TEXTFORMATPRIMITIVES.H
============================================================================================================================================*/
// 🧩 Canonical ASCII casing, whitespace trimming, and hexadecimal nibble parsing primitives shared across codecs and UI

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   ASCII STRING PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::string LowercaseAscii(std::string_view Text)
{
    std::string Output;
    Output.reserve(Text.size());
    for (const unsigned char Character : Text)
    {
        Output.push_back(static_cast<char>(std::tolower(Character)));
    }
    return Output;
}

[[nodiscard]] inline std::string_view TrimAsciiView(std::string_view Text) noexcept
{
    std::size_t First = 0u;
    std::size_t Last  = Text.size();
    while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])) != 0)
    {
        ++First;
    }
    while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1u])) != 0)
    {
        --Last;
    }
    return Text.substr(First, Last - First);
}

[[nodiscard]] inline std::string TrimAsciiWhitespace(std::string_view Text)
{
    return std::string(TrimAsciiView(Text));
}

[[nodiscard]] constexpr int ParseHexNibble(char Character) noexcept
{
    if (Character >= '0' && Character <= '9')
    {
        return Character - '0';
    }
    if (Character >= 'a' && Character <= 'f')
    {
        return 10 + (Character - 'a');
    }
    if (Character >= 'A' && Character <= 'F')
    {
        return 10 + (Character - 'A');
    }
    return -1;
}

} // namespace Frontier::Serialization
