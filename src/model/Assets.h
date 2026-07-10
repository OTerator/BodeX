#pragma once

// Image asset storage: attached images are copied into a folder beside the
// project's .json (e.g. Exam1.assets/) so they travel with the project. Before a
// project is first saved, its images live in a per-project staging folder under
// %APPDATA%\BodeX\staging\<id>. GUI-free (Win32 file ops only), like AppConfig.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "model/Project.h"

namespace gt {

// Wrap a Windows DIB (BITMAPINFOHEADER + optional color-table/masks + pixel bits,
// exactly what CF_DIB holds) into a complete .bmp file image by prepending the
// 14-byte BITMAPFILEHEADER. Pure byte math — no Win32, no pixel conversion — so it
// is unit-tested and stb_image decodes the result. `dib` points at the DIB header;
// `n` is its total size. Returns false on a malformed/too-small DIB.
inline bool buildBmpFromDib(const uint8_t* dib, size_t n, std::vector<uint8_t>& out)
{
    if (!dib || n < 40)                       // need at least a BITMAPINFOHEADER
        return false;

    auto u16 = [&](size_t o) -> uint32_t { return dib[o] | (dib[o + 1] << 8); };
    auto u32 = [&](size_t o) -> uint32_t {
        return static_cast<uint32_t>(dib[o]) | (static_cast<uint32_t>(dib[o + 1]) << 8) |
               (static_cast<uint32_t>(dib[o + 2]) << 16) | (static_cast<uint32_t>(dib[o + 3]) << 24);
    };

    const uint32_t biSize      = u32(0);
    const uint32_t biBitCount  = u16(14);
    const uint32_t biCompress  = u32(16);
    uint32_t       biClrUsed   = u32(32);
    if (biSize < 40 || biSize > n)
        return false;

    // Bytes between the header and the pixel bits: bitfield masks (BI_BITFIELDS on a
    // 40-byte header) or a palette (<=8bpp). V4/V5 headers carry masks inside biSize.
    uint32_t extra = 0;
    if (biCompress == 3 /*BI_BITFIELDS*/ && biSize == 40) {
        extra = 12;                            // three DWORD color masks
    } else if (biBitCount <= 8) {
        if (biClrUsed == 0) biClrUsed = 1u << biBitCount;
        extra = biClrUsed * 4;
    }

    const uint32_t offBits = 14 + biSize + extra;
    const uint32_t fileSize = 14 + static_cast<uint32_t>(n);
    if (offBits > fileSize)
        return false;

    out.clear();
    out.resize(fileSize);
    out[0] = 'B'; out[1] = 'M';
    std::memcpy(&out[2], &fileSize, 4);        // bfSize
    out[6] = out[7] = out[8] = out[9] = 0;     // bfReserved1/2
    std::memcpy(&out[10], &offBits, 4);        // bfOffBits
    std::memcpy(&out[14], dib, n);
    return true;
}

// dir(projectPath)/stem(projectPath).assets — created on demand. "" if no path.
std::string projectAssetsDir(const std::string& projectPath);

// %APPDATA%\BodeX\staging\<projectId> — created on demand.
std::string stagingAssetsDir(const std::string& projectId);

// Where the project's image files currently live: project assets if saved, else
// the staging dir.
std::string liveAssetsDir(const Project& p, const std::string& projectPath);

// Copy srcPath into destDir under a fresh unique filename; returns the stored
// filename (e.g. "img-ab12...png"), or "" on failure.
std::string importImage(const std::string& srcPath, const std::string& destDir);

// Pull an image off the Windows clipboard into a temp file and return its path
// ("" if the clipboard holds no image). A bitmap (CF_DIB) is written as a .bmp via
// `buildBmpFromDib`; a copied image file (CF_HDROP) yields that file's path. The
// caller feeds the result through `importImage` like a picked file, then deletes it.
std::string clipboardImageToTempFile(const std::string& tempDir);

// Ensure each listed filename exists in newDir, copying from fromDir when
// missing (used to migrate assets on Save / Save As).
void syncImages(const std::string& fromDir, const std::string& newDir,
                const std::vector<std::string>& files);

} // namespace gt
