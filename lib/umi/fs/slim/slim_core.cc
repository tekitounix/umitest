// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix
//
// slimfs — compact, power-loss safe embedded filesystem.

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "slim.hh"

namespace umi::fs {
namespace {

// =====================================================================
// CRC-32 (polynomial 0x04C11DB7, 4-bit table, constexpr)
// =====================================================================

constexpr uint32_t CRC32_POLY = 0x04C11DB7;

constexpr auto make_crc32_table() {
    std::array<uint32_t, 16> t{};
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t c = i << 28;
        for (int j = 0; j < 4; ++j)
            c = (c & 0x80000000) ? (c << 1) ^ CRC32_POLY : (c << 1);
        t[i] = c;
    }
    return t;
}

constexpr auto crc32_table = make_crc32_table();

constexpr uint32_t crc32(uint32_t crc, const void* data, uint32_t size) {
    auto p = static_cast<const uint8_t*>(data);
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(p[i]) << 24;
        crc = (crc << 4) ^ crc32_table[crc >> 28];
        crc = (crc << 4) ^ crc32_table[crc >> 28];
    }
    return crc;
}

// =====================================================================
// Little-endian helpers
// =====================================================================

constexpr uint32_t ld_u32(const void* p) {
    auto b = static_cast<const uint8_t*>(p);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

constexpr void st_u32(void* p, uint32_t v) {
    auto b = static_cast<uint8_t*>(p);
    b[0] = static_cast<uint8_t>(v);
    b[1] = static_cast<uint8_t>(v >> 8);
    b[2] = static_cast<uint8_t>(v >> 16);
    b[3] = static_cast<uint8_t>(v >> 24);
}

// =====================================================================
// Error helper
// =====================================================================

constexpr int err(SlimError e) {
    return static_cast<int>(e);
}

// =====================================================================
// Block device wrappers
// =====================================================================

int bd_read(SlimFs& fs, uint32_t block, uint32_t off, void* buf, uint32_t size) {
    return fs.cfg->read(fs.cfg, block, off, buf, size);
}

int bd_prog(SlimFs& fs, uint32_t block, uint32_t off, const void* buf, uint32_t size) {
    // Invalidate read cache for this block to prevent stale reads
    if (fs.rcache.valid() && fs.rcache.block == block)
        fs.rcache.drop();
    return fs.cfg->prog(fs.cfg, block, off, buf, size);
}

int bd_erase(SlimFs& fs, uint32_t block) {
    if (fs.rcache.valid() && fs.rcache.block == block)
        fs.rcache.drop();
    return fs.cfg->erase(fs.cfg, block);
}

int bd_sync(SlimFs& fs) {
    return fs.cfg->sync(fs.cfg);
}

// =====================================================================
// Cache operations
// =====================================================================

void cache_drop(SlimFs::Cache& c) {
    c.drop();
}

int cache_read(SlimFs& fs, SlimFs::Cache& cache, uint32_t block, uint32_t off, void* buf, uint32_t size) {
    auto dst = static_cast<uint8_t*>(buf);
    while (size > 0) {
        // Check cache hit
        if (cache.valid() && cache.block == block && off >= cache.off && off + size <= cache.off + cache.size) {
            std::memcpy(dst, cache.buffer.data() + (off - cache.off), size);
            return 0;
        }
        // Cache miss — read full block into cache
        uint32_t rd_size = fs.cfg->block_size;
        if (rd_size > cache.buffer.size())
            rd_size = static_cast<uint32_t>(cache.buffer.size());
        int rc = bd_read(fs, block, 0, cache.buffer.data(), rd_size);
        if (rc)
            return rc;
        cache.block = block;
        cache.off = 0;
        cache.size = rd_size;
    }
    return 0;
}

int cache_flush(SlimFs& fs, SlimFs::Cache& cache) {
    if (!cache.valid() || cache.size == 0)
        return 0;
    int rc = bd_prog(fs, cache.block, cache.off, cache.buffer.data(), cache.size);
    if (rc)
        return rc;
    cache_drop(cache);
    return 0;
}

// =====================================================================
// Superblock read/write
// =====================================================================

// On-disk superblock layout (64 bytes):
// [0..3]   magic
// [4..7]   version
// [8..11]  block_size
// [12..15] block_count
// [16..19] name_max
// [20..23] file_max
// [24..27] root_block
// [28..31] alloc_next
// [32..35] alloc_seed
// [36..43] pending_move (src_dir, src_id<<16|dst_id, dst_dir)
// [44..59] reserved
// [60..63] crc32

struct SuperblockData {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t name_max;
    uint32_t file_max;
    uint32_t root_block;
    uint32_t alloc_next;
    uint32_t alloc_seed;
    uint32_t pm_src_dir;
    uint32_t pm_ids; // src_id<<16 | dst_id
    uint32_t pm_dst_dir;
};

int superblock_write(SlimFs& fs, uint32_t sb_block) {
    uint8_t buf[SLIM_SUPER_SIZE]{};
    st_u32(buf + 0, SLIM_MAGIC);
    st_u32(buf + 4, SLIM_VERSION);
    st_u32(buf + 8, fs.cfg->block_size);
    st_u32(buf + 12, fs.block_count);
    st_u32(buf + 16, fs.name_max);
    st_u32(buf + 20, fs.file_max);
    st_u32(buf + 24, fs.root_block);
    st_u32(buf + 28, fs.alloc_next);
    st_u32(buf + 32, fs.alloc_seed);
    // pending_move
    st_u32(buf + 36, fs.pending_move.src_dir);
    st_u32(buf + 40, (static_cast<uint32_t>(fs.pending_move.src_id) << 16) | fs.pending_move.dst_id);
    st_u32(buf + 44, fs.pending_move.dst_dir);
    // CRC over first 60 bytes
    uint32_t c = crc32(0xFFFFFFFF, buf, SLIM_SUPER_SIZE - 4);
    st_u32(buf + 60, c);

    int rc = bd_erase(fs, sb_block);
    if (rc)
        return rc;
    rc = bd_prog(fs, sb_block, 0, buf, SLIM_SUPER_SIZE);
    if (rc)
        return rc;
    return bd_sync(fs);
}

int superblock_read(SlimFs& fs, uint32_t sb_block, bool& valid) {
    uint8_t buf[SLIM_SUPER_SIZE]{};
    int rc = bd_read(fs, sb_block, 0, buf, SLIM_SUPER_SIZE);
    if (rc) {
        valid = false;
        return rc;
    }

    // Verify CRC
    uint32_t c = crc32(0xFFFFFFFF, buf, SLIM_SUPER_SIZE - 4);
    if (c != ld_u32(buf + 60)) {
        valid = false;
        return 0;
    }

    // Verify magic and version
    if (ld_u32(buf + 0) != SLIM_MAGIC) {
        valid = false;
        return 0;
    }
    uint32_t ver = ld_u32(buf + 4);
    if ((ver >> 16) != (SLIM_VERSION >> 16)) {
        valid = false;
        return 0;
    }

    valid = true;
    fs.block_count = ld_u32(buf + 12);
    fs.name_max = ld_u32(buf + 16);
    fs.file_max = ld_u32(buf + 20);
    fs.root_block = ld_u32(buf + 24);
    fs.alloc_next = ld_u32(buf + 28);
    fs.alloc_seed = ld_u32(buf + 32);
    // pending_move
    fs.pending_move.src_dir = ld_u32(buf + 36);
    uint32_t ids = ld_u32(buf + 40);
    fs.pending_move.src_id = static_cast<uint16_t>(ids >> 16);
    fs.pending_move.dst_id = static_cast<uint16_t>(ids & 0xFFFF);
    fs.pending_move.dst_dir = ld_u32(buf + 44);

    return 0;
}

// =====================================================================
// Metadata block operations
// =====================================================================

// Metadata header layout (12 bytes):
// [0..3]   revision
// [4..7]   next_block
// [8..11]  entry_count

int meta_write_empty(SlimFs& fs, uint32_t block, uint32_t revision) {
    int rc = bd_erase(fs, block);
    if (rc)
        return rc;

    uint8_t hdr[SLIM_META_HEADER_SIZE]{};
    st_u32(hdr + 0, revision);
    st_u32(hdr + 4, SLIM_BLOCK_NULL);
    st_u32(hdr + 8, 0); // entry_count = 0

    rc = bd_prog(fs, block, 0, hdr, SLIM_META_HEADER_SIZE);
    if (rc)
        return rc;

    // CRC at offset SLIM_META_HEADER_SIZE
    uint32_t c = crc32(0xFFFFFFFF, hdr, SLIM_META_HEADER_SIZE);
    uint8_t crc_buf[4];
    st_u32(crc_buf, c);
    rc = bd_prog(fs, block, SLIM_META_HEADER_SIZE, crc_buf, 4);
    if (rc)
        return rc;

    return bd_sync(fs);
}

int meta_read_header(SlimFs& fs, uint32_t block, uint32_t& revision, uint32_t& next_block, uint32_t& entry_count) {
    uint8_t hdr[SLIM_META_HEADER_SIZE];
    int rc = cache_read(fs, fs.rcache, block, 0, hdr, SLIM_META_HEADER_SIZE);
    if (rc)
        return rc;
    revision = ld_u32(hdr + 0);
    next_block = ld_u32(hdr + 4);
    entry_count = ld_u32(hdr + 8);
    // Sanity: max entries bounded by block size (each entry >= SLIM_ENTRY_HEADER_SIZE + 1 byte name)
    uint32_t max_entries = (fs.cfg->block_size - SLIM_META_HEADER_SIZE - 4) / (SLIM_ENTRY_HEADER_SIZE + 1);
    if (entry_count > max_entries)
        return err(SlimError::CORRUPT);
    return 0;
}

// Entry layout (variable length):
// [0]     type
// [1]     name_len
// [2..3]  reserved
// [4..7]  size
// [8..11] head_block
// [12..15] attr_size
// [16..16+name_len-1] name
// [16+name_len..16+name_len+attr_size-1] attrs

struct EntryHeader {
    SlimType type;
    uint8_t name_len;
    uint32_t size;
    uint32_t head_block;
    uint32_t attr_size;
};

uint32_t entry_total_size(const EntryHeader& eh) {
    return SLIM_ENTRY_HEADER_SIZE + eh.name_len + eh.attr_size;
}

int entry_read_header(SlimFs& fs, uint32_t block, uint32_t off, EntryHeader& eh) {
    if (off + SLIM_ENTRY_HEADER_SIZE > fs.cfg->block_size)
        return err(SlimError::CORRUPT);
    uint8_t buf[SLIM_ENTRY_HEADER_SIZE];
    int rc = cache_read(fs, fs.rcache, block, off, buf, SLIM_ENTRY_HEADER_SIZE);
    if (rc)
        return rc;
    eh.type = static_cast<SlimType>(buf[0]);
    eh.name_len = buf[1];
    eh.size = ld_u32(buf + 4);
    eh.head_block = ld_u32(buf + 8);
    eh.attr_size = ld_u32(buf + 12);
    // Sanity check: entry must fit in block
    if (off + entry_total_size(eh) + 4 > fs.cfg->block_size)
        return err(SlimError::CORRUPT);
    return 0;
}

int entry_read_name(
    SlimFs& fs, uint32_t block, uint32_t off, const EntryHeader& eh, char* name, uint32_t name_buf_size) {
    uint32_t len = eh.name_len;
    if (len >= name_buf_size)
        len = name_buf_size - 1;
    int rc = cache_read(fs, fs.rcache, block, off + SLIM_ENTRY_HEADER_SIZE, name, len);
    if (rc)
        return rc;
    name[len] = '\0';
    return 0;
}

// =====================================================================
// Block allocator (forward declarations for rescan)
// =====================================================================

void mark_alloc_used(SlimFs& fs, uint32_t b);
int traverse_dir_blocks(SlimFs& fs, uint32_t dir_block, void (*mark)(SlimFs&, uint32_t));

// =====================================================================
// Block allocator
// =====================================================================

int alloc_block(SlimFs& fs, uint32_t& out) {
    auto& la = fs.lookahead;

    // Advance alloc_seed (LCG) to distribute allocations across blocks
    fs.alloc_seed = fs.alloc_seed * 1103515245 + 12345;

    // Try from la.next first, then wrap around the full lookahead window
    for (uint32_t attempt = 0; attempt < la.size; ++attempt) {
        uint32_t i = (la.next + attempt) % la.size;
        if (!(la.buffer[i / 8] & (1u << (i % 8)))) {
            out = (la.start + i) % fs.block_count;
            la.buffer[i / 8] |= (1u << (i % 8));
            la.next = (i + 1) % la.size;
            fs.alloc_next = (out + 1) % fs.block_count;
            return 0;
        }
    }

    // Lookahead window exhausted — rescan from alloc_next with seed-based offset
    uint32_t scan_start = (fs.alloc_next + ((fs.alloc_seed >> 16) % fs.block_count)) % fs.block_count;
    la.start = scan_start;
    la.size = static_cast<uint32_t>(la.buffer.size()) * 8;
    if (la.size > fs.block_count)
        la.size = fs.block_count;
    la.next = 0;
    std::memset(la.buffer.data(), 0, la.buffer.size());

    // Mark superblocks as used
    mark_alloc_used(fs, SLIM_SUPER_BLOCK_A);
    mark_alloc_used(fs, SLIM_SUPER_BLOCK_B);

    // Rescan all used blocks
    int rc = traverse_dir_blocks(fs, fs.root_block, mark_alloc_used);
    if (rc)
        return rc;

    // Retry allocation in new window
    for (uint32_t i = 0; i < la.size; ++i) {
        if (!(la.buffer[i / 8] & (1u << (i % 8)))) {
            out = (la.start + i) % fs.block_count;
            la.buffer[i / 8] |= (1u << (i % 8));
            la.next = (i + 1) % la.size;
            fs.alloc_next = (out + 1) % fs.block_count;
            return 0;
        }
    }
    return err(SlimError::NOSPC);
}

// =====================================================================
// Path parsing
// =====================================================================

struct PathSegment {
    const char* name;
    uint32_t len;
};

PathSegment path_next(const char* path) {
    // Skip leading slashes
    while (*path == '/')
        ++path;
    const char* start = path;
    while (*path && *path != '/')
        ++path;
    return {start, static_cast<uint32_t>(path - start)};
}

const char* path_advance(const char* path) {
    while (*path == '/')
        ++path;
    while (*path && *path != '/')
        ++path;
    while (*path == '/')
        ++path;
    return path;
}

// =====================================================================
// Directory entry search
// =====================================================================

struct FindResult {
    uint32_t block;
    uint32_t entry_off;
    uint32_t entry_index;
    EntryHeader header;
};

int dir_find_entry(SlimFs& fs, uint32_t dir_block, const char* name, uint32_t name_len, FindResult& result) {
    uint32_t revision, next_block, entry_count;
    int rc = meta_read_header(fs, dir_block, revision, next_block, entry_count);
    if (rc)
        return rc;

    uint32_t off = SLIM_META_HEADER_SIZE;
    for (uint32_t i = 0; i < entry_count; ++i) {
        EntryHeader eh;
        rc = entry_read_header(fs, dir_block, off, eh);
        if (rc)
            return rc;

        if (eh.type != SlimType::DEL && eh.name_len == name_len) {
            char entry_name[256];
            rc = entry_read_name(fs, dir_block, off, eh, entry_name, sizeof(entry_name));
            if (rc)
                return rc;
            if (std::memcmp(entry_name, name, name_len) == 0) {
                result.block = dir_block;
                result.entry_off = off;
                result.entry_index = i;
                result.header = eh;
                return 0;
            }
        }
        off += entry_total_size(eh);
    }

    // Check continuation block
    if (next_block != SLIM_BLOCK_NULL) {
        return dir_find_entry(fs, next_block, name, name_len, result);
    }

    return err(SlimError::NOENT);
}

// Find entry by index (used for pending_move recovery)
int dir_find_entry_by_index(SlimFs& fs, uint32_t dir_block, uint32_t target_index, FindResult& result) {
    uint32_t revision, next_block, entry_count;
    int rc = meta_read_header(fs, dir_block, revision, next_block, entry_count);
    if (rc)
        return rc;

    uint32_t off = SLIM_META_HEADER_SIZE;
    for (uint32_t i = 0; i < entry_count; ++i) {
        EntryHeader eh;
        rc = entry_read_header(fs, dir_block, off, eh);
        if (rc)
            return rc;
        if (i == target_index && eh.type != SlimType::DEL) {
            result.block = dir_block;
            result.entry_off = off;
            result.entry_index = i;
            result.header = eh;
            return 0;
        }
        off += entry_total_size(eh);
    }
    return err(SlimError::NOENT);
}

// Follow a full path, returning the final entry's directory block and entry info
int path_resolve(SlimFs& fs, const char* path, FindResult& result) {
    if (!path || !*path)
        return err(SlimError::INVAL);

    // Skip leading slash
    while (*path == '/')
        ++path;
    if (!*path) {
        // Root directory itself
        result.block = fs.root_block;
        result.entry_off = 0;
        result.entry_index = 0;
        result.header.type = SlimType::DIR;
        result.header.name_len = 0;
        result.header.size = 0;
        result.header.head_block = fs.root_block;
        result.header.attr_size = 0;
        return 0;
    }

    uint32_t cur_dir = fs.root_block;
    while (*path) {
        auto seg = path_next(path);
        if (seg.len == 0)
            break;
        if (seg.len > fs.name_max)
            return err(SlimError::NAMETOOLONG);

        int rc = dir_find_entry(fs, cur_dir, seg.name, seg.len, result);
        if (rc)
            return rc;

        path = path_advance(path);
        if (*path) {
            // More segments — this must be a directory
            if (result.header.type != SlimType::DIR)
                return err(SlimError::NOTDIR);
            cur_dir = result.header.head_block;
        }
    }
    return 0;
}

// Resolve parent directory: returns parent dir block and the final name segment
int path_resolve_parent(SlimFs& fs, const char* path, uint32_t& parent_block, const char*& name, uint32_t& name_len) {
    if (!path || !*path)
        return err(SlimError::INVAL);
    while (*path == '/')
        ++path;
    if (!*path)
        return err(SlimError::INVAL);

    // Find last segment
    const char* last_seg = path;
    const char* p = path;
    while (*p) {
        path_next(p); // advance past current segment
        const char* next = path_advance(p);
        if (!*next) {
            last_seg = p;
            break;
        }
        p = next;
    }

    name = last_seg;
    while (*name == '/')
        ++name;
    const char* end = name;
    while (*end && *end != '/')
        ++end;
    name_len = static_cast<uint32_t>(end - name);

    if (name_len > fs.name_max)
        return err(SlimError::NAMETOOLONG);

    // Resolve parent (everything before last segment)
    if (last_seg == path) {
        // Parent is root
        parent_block = fs.root_block;
        return 0;
    }

    // Walk to parent
    uint32_t cur_dir = fs.root_block;
    p = path;
    while (p < last_seg) {
        while (*p == '/')
            ++p;
        if (p >= last_seg)
            break;
        auto seg = path_next(p);
        if (seg.len == 0)
            break;

        FindResult fr;
        int rc = dir_find_entry(fs, cur_dir, seg.name, seg.len, fr);
        if (rc)
            return rc;
        if (fr.header.type != SlimType::DIR)
            return err(SlimError::NOTDIR);
        cur_dir = fr.header.head_block;
        p = path_advance(p);
    }
    parent_block = cur_dir;
    return 0;
}

// =====================================================================
// Metadata commit helpers
// =====================================================================

// Calculate total used space in a metadata block (header + entries + crc)
int meta_used_size(SlimFs& fs, uint32_t block, uint32_t& used) {
    uint32_t revision, next_block, entry_count;
    int rc = meta_read_header(fs, block, revision, next_block, entry_count);
    if (rc)
        return rc;

    uint32_t off = SLIM_META_HEADER_SIZE;
    for (uint32_t i = 0; i < entry_count; ++i) {
        EntryHeader eh;
        rc = entry_read_header(fs, block, off, eh);
        if (rc)
            return rc;
        off += entry_total_size(eh);
    }
    used = off + 4; // +4 for CRC
    return 0;
}

// Write CRC at the given offset, covering [0..off)
// Append an entry to a metadata block. Returns error if no space.
int meta_append_entry(SlimFs& fs,
                      uint32_t block,
                      SlimType type,
                      const char* name,
                      uint32_t name_len,
                      uint32_t size,
                      uint32_t head_block,
                      uint32_t attr_size = 0) {
    uint32_t used;
    int rc = meta_used_size(fs, block, used);
    if (rc)
        return rc;

    uint32_t entry_size = SLIM_ENTRY_HEADER_SIZE + name_len + attr_size;
    uint32_t new_used = (used - 4) + entry_size + 4; // remove old crc, add entry + new crc
    if (new_used > fs.cfg->block_size)
        return err(SlimError::NOSPC);

    // Read current header to update entry_count and revision
    uint32_t revision, next_blk, entry_count;
    rc = meta_read_header(fs, block, revision, next_blk, entry_count);
    if (rc)
        return rc;

    // Erase and rewrite block (COW: increment revision)
    // Read entire block content first
    cache_drop(fs.rcache);
    uint8_t* buf = fs.pcache.buffer.data();
    uint32_t old_data_end = used - 4;
    rc = bd_read(fs, block, 0, buf, old_data_end);
    if (rc)
        return rc;

    rc = bd_erase(fs, block);
    if (rc)
        return rc;

    // Update header: revision+1, entry_count+1
    st_u32(buf + 0, revision + 1);
    st_u32(buf + 8, entry_count + 1);

    // Write existing data
    rc = bd_prog(fs, block, 0, buf, old_data_end);
    if (rc)
        return rc;

    // Write new entry
    uint8_t ehdr[SLIM_ENTRY_HEADER_SIZE]{};
    ehdr[0] = static_cast<uint8_t>(type);
    ehdr[1] = static_cast<uint8_t>(name_len);
    st_u32(ehdr + 4, size);
    st_u32(ehdr + 8, head_block);
    st_u32(ehdr + 12, attr_size);

    rc = bd_prog(fs, block, old_data_end, ehdr, SLIM_ENTRY_HEADER_SIZE);
    if (rc)
        return rc;

    if (name_len > 0) {
        rc = bd_prog(fs, block, old_data_end + SLIM_ENTRY_HEADER_SIZE, name, name_len);
        if (rc)
            return rc;
    }

    // Write CRC
    uint32_t new_data_end = old_data_end + entry_size;
    // Need to re-read for CRC computation
    cache_drop(fs.rcache);
    uint32_t crc_val = 0xFFFFFFFF;
    uint32_t crc_pos = 0;
    uint8_t tmp[64];
    while (crc_pos < new_data_end) {
        uint32_t chunk = std::min(new_data_end - crc_pos, static_cast<uint32_t>(sizeof(tmp)));
        rc = bd_read(fs, block, crc_pos, tmp, chunk);
        if (rc)
            return rc;
        crc_val = crc32(crc_val, tmp, chunk);
        crc_pos += chunk;
    }
    uint8_t crc_buf[4];
    st_u32(crc_buf, crc_val);
    rc = bd_prog(fs, block, new_data_end, crc_buf, 4);
    if (rc)
        return rc;

    return bd_sync(fs);
}

// Mark entry as deleted by setting type to DEL
int meta_delete_entry(SlimFs& fs, uint32_t block, uint32_t entry_off) {
    // Read current header
    uint32_t revision, next_blk, entry_count;
    int rc = meta_read_header(fs, block, revision, next_blk, entry_count);
    if (rc)
        return rc;

    cache_drop(fs.rcache);

    // Re-read since we dropped cache
    rc = meta_read_header(fs, block, revision, next_blk, entry_count);
    if (rc)
        return rc;

    // Just overwrite the type byte to DEL and recompute CRC
    // This requires re-erasing and rewriting the block (flash constraint)
    uint32_t total;
    rc = meta_used_size(fs, block, total);
    if (rc)
        return rc;
    uint32_t data_end = total - 4;

    uint8_t* buf = fs.pcache.buffer.data();
    rc = bd_read(fs, block, 0, buf, data_end);
    if (rc)
        return rc;

    // Mark as deleted
    buf[entry_off] = static_cast<uint8_t>(SlimType::DEL);
    // Increment revision
    uint32_t rev = ld_u32(buf);
    st_u32(buf, rev + 1);

    rc = bd_erase(fs, block);
    if (rc)
        return rc;
    rc = bd_prog(fs, block, 0, buf, data_end);
    if (rc)
        return rc;

    // CRC
    uint32_t crc_val = crc32(0xFFFFFFFF, buf, data_end);
    uint8_t crc_buf[4];
    st_u32(crc_buf, crc_val);
    rc = bd_prog(fs, block, data_end, crc_buf, 4);
    if (rc)
        return rc;

    cache_drop(fs.rcache);
    return bd_sync(fs);
}

// Update entry fields (size, head_block) in-place
int meta_update_entry(SlimFs& fs, uint32_t block, uint32_t entry_off, uint32_t new_size, uint32_t new_head_block) {
    uint32_t revision, next_blk, entry_count;
    int rc = meta_read_header(fs, block, revision, next_blk, entry_count);
    if (rc)
        return rc;

    uint32_t total;
    rc = meta_used_size(fs, block, total);
    if (rc)
        return rc;
    uint32_t data_end = total - 4;

    uint8_t* buf = fs.pcache.buffer.data();
    rc = bd_read(fs, block, 0, buf, data_end);
    if (rc)
        return rc;

    // Update fields
    st_u32(buf + entry_off + 4, new_size);
    st_u32(buf + entry_off + 8, new_head_block);
    // Increment revision
    uint32_t rev = ld_u32(buf);
    st_u32(buf, rev + 1);

    cache_drop(fs.rcache);

    rc = bd_erase(fs, block);
    if (rc)
        return rc;
    rc = bd_prog(fs, block, 0, buf, data_end);
    if (rc)
        return rc;

    uint32_t crc_val = crc32(0xFFFFFFFF, buf, data_end);
    uint8_t crc_buf[4];
    st_u32(crc_buf, crc_val);
    rc = bd_prog(fs, block, data_end, crc_buf, 4);
    if (rc)
        return rc;

    return bd_sync(fs);
}

// =====================================================================
// Block chain helpers (for large files)
// =====================================================================

// Each data block: [0..block_size-5] = data, [block_size-4..block_size-1] = next_block (LE32)
// Terminal: next = SLIM_BLOCK_NULL

uint32_t chain_data_size(uint32_t block_size) {
    return block_size - 4;
}

int chain_read_next(SlimFs& fs, uint32_t block, uint32_t& next) {
    uint8_t buf[4];
    int rc = cache_read(fs, fs.rcache, block, fs.cfg->block_size - 4, buf, 4);
    if (rc)
        return rc;
    next = ld_u32(buf);
    return 0;
}

int chain_write_next(SlimFs& fs, uint32_t block, uint32_t next) {
    uint8_t buf[4];
    st_u32(buf, next);
    return bd_prog(fs, block, fs.cfg->block_size - 4, buf, 4);
}

// Walk a chain to find the block at a given file offset
int chain_walk(SlimFs& fs, uint32_t head, uint32_t target_off, uint32_t& out_block, uint32_t& out_off_in_block) {
    uint32_t data_per_block = chain_data_size(fs.cfg->block_size);
    uint32_t block = head;
    uint32_t remaining = target_off;

    while (remaining >= data_per_block) {
        uint32_t next;
        int rc = chain_read_next(fs, block, next);
        if (rc)
            return rc;
        if (next == SLIM_BLOCK_NULL)
            return err(SlimError::CORRUPT);
        block = next;
        remaining -= data_per_block;
    }
    out_block = block;
    out_off_in_block = remaining;
    return 0;
}

// Free a chain starting from head
int chain_free(SlimFs& fs, uint32_t head) {
    uint32_t block = head;
    uint32_t safety = fs.block_count;
    while (block != SLIM_BLOCK_NULL && safety > 0) {
        uint32_t next;
        int rc = chain_read_next(fs, block, next);
        if (rc)
            return rc;
        // Block is now free (no need to erase — allocator will erase on reuse)
        block = next;
        --safety;
    }
    if (safety == 0)
        return err(SlimError::CORRUPT);
    return 0;
}

// =====================================================================
// Full traverse for alloc_scan (marks all used blocks)
// =====================================================================

int traverse_dir_blocks(SlimFs& fs, uint32_t dir_block, void (*mark)(SlimFs&, uint32_t)) {
    uint32_t block = dir_block;
    uint32_t safety = fs.block_count;
    while (block != SLIM_BLOCK_NULL && safety > 0) {
        mark(fs, block);

        uint32_t revision, next_block, entry_count;
        int rc = meta_read_header(fs, block, revision, next_block, entry_count);
        if (rc)
            return rc;

        // Scan entries for file data blocks and subdirectories
        uint32_t off = SLIM_META_HEADER_SIZE;
        for (uint32_t i = 0; i < entry_count; ++i) {
            EntryHeader eh;
            rc = entry_read_header(fs, block, off, eh);
            if (rc)
                return rc;

            if (eh.type != SlimType::DEL && eh.head_block != SLIM_BLOCK_NULL && eh.head_block != SLIM_BLOCK_INLINE) {
                if (eh.type == SlimType::DIR) {
                    // Recurse into subdirectory — use iterative approach with bounded depth
                    // For simplicity in v1, we do recursive call with safety counter
                    rc = traverse_dir_blocks(fs, eh.head_block, mark);
                    if (rc)
                        return rc;
                } else {
                    // File — walk chain
                    uint32_t fb = eh.head_block;
                    uint32_t fsafety = fs.block_count;
                    while (fb != SLIM_BLOCK_NULL && fsafety > 0) {
                        mark(fs, fb);
                        uint32_t next;
                        rc = chain_read_next(fs, fb, next);
                        if (rc)
                            return rc;
                        fb = next;
                        --fsafety;
                    }
                }
            }
            off += entry_total_size(eh);
        }

        block = next_block;
        --safety;
    }
    return 0;
}

void mark_alloc_used(SlimFs& fs, uint32_t b) {
    auto& la = fs.lookahead;
    if (b >= fs.block_count)
        return;
    uint32_t rel = (b - la.start + fs.block_count) % fs.block_count;
    if (rel < la.size) {
        la.buffer[rel / 8] |= (1u << (rel % 8));
    }
}

int alloc_full_scan(SlimFs& fs) {
    auto& la = fs.lookahead;
    la.start = fs.alloc_next;
    la.size = static_cast<uint32_t>(la.buffer.size()) * 8;
    if (la.size > fs.block_count)
        la.size = fs.block_count;
    la.next = 0;
    std::memset(la.buffer.data(), 0, la.buffer.size());

    mark_alloc_used(fs, SLIM_SUPER_BLOCK_A);
    mark_alloc_used(fs, SLIM_SUPER_BLOCK_B);

    return traverse_dir_blocks(fs, fs.root_block, mark_alloc_used);
}

} // anonymous namespace

// =====================================================================
// Public API: format
// =====================================================================

int SlimFs::format(const SlimConfig* config) noexcept {
    if (!config || !config->read || !config->prog || !config->erase || !config->sync)
        return err(SlimError::INVAL);
    if (config->block_count < 4)
        return err(SlimError::INVAL);
    if (config->block_size < SLIM_SUPER_SIZE + SLIM_META_HEADER_SIZE + 4)
        return err(SlimError::INVAL);

    cfg = config;
    block_count = config->block_count;
    name_max = config->name_max ? config->name_max : SLIM_NAME_MAX_DEFAULT;
    file_max = config->file_max ? config->file_max : SLIM_FILE_MAX_DEFAULT;
    root_block = 2; // First data block after superblock pair
    alloc_next = 3;
    alloc_seed = 0;
    pending_move.clear();

    rcache = Cache{};
    pcache = Cache{};
    rcache.buffer = config->read_buffer;
    pcache.buffer = config->prog_buffer;

    // Erase and write root directory (empty)
    int rc = meta_write_empty(*this, root_block, 0);
    if (rc)
        return rc;

    // Write superblock pair (both copies)
    rc = superblock_write(*this, SLIM_SUPER_BLOCK_A);
    if (rc)
        return rc;
    rc = superblock_write(*this, SLIM_SUPER_BLOCK_B);
    if (rc)
        return rc;

    return 0;
}

// =====================================================================
// Public API: mount
// =====================================================================

int SlimFs::mount(const SlimConfig* config) noexcept {
    if (!config || !config->read || !config->prog || !config->erase || !config->sync)
        return err(SlimError::INVAL);

    cfg = config;
    rcache = Cache{};
    pcache = Cache{};
    rcache.buffer = config->read_buffer;
    pcache.buffer = config->prog_buffer;
    lookahead.buffer = config->lookahead_buffer;

    // Try superblock A, then B
    bool valid_a = false, valid_b = false;
    int rc = superblock_read(*this, SLIM_SUPER_BLOCK_A, valid_a);
    if (rc)
        return rc;
    if (!valid_a) {
        rc = superblock_read(*this, SLIM_SUPER_BLOCK_B, valid_b);
        if (rc)
            return rc;
        if (!valid_b)
            return err(SlimError::CORRUPT);
    }

    // Verify block_size matches
    if (config->block_size != 0 && config->block_count != 0) {
        // Config provides expected values
    }

    name_max = name_max ? name_max : SLIM_NAME_MAX_DEFAULT;
    file_max = file_max ? file_max : SLIM_FILE_MAX_DEFAULT;

    // Handle pending_move recovery: complete or roll back interrupted rename
    if (pending_move.active()) {
        // Check if dst entry was written (rename's meta_append_entry completed)
        FindResult dst_fr;
        int pm_rc = dir_find_entry_by_index(*this, pending_move.dst_dir, pending_move.dst_id, dst_fr);
        if (pm_rc == 0) {
            // dst exists → rename append succeeded, delete src to complete
            FindResult src_fr;
            pm_rc = dir_find_entry_by_index(*this, pending_move.src_dir, pending_move.src_id, src_fr);
            if (pm_rc == 0) {
                (void)meta_delete_entry(*this, src_fr.block, src_fr.entry_off);
            }
        }
        // Either way, clear pending_move and persist
        pending_move.clear();
        (void)superblock_write(*this, SLIM_SUPER_BLOCK_A);
        (void)superblock_write(*this, SLIM_SUPER_BLOCK_B);
    }

    // Full allocation scan
    rc = alloc_full_scan(*this);
    if (rc)
        return rc;

    return 0;
}

// =====================================================================
// Public API: unmount
// =====================================================================

int SlimFs::unmount() noexcept {
    // Flush caches
    int rc = cache_flush(*this, pcache);
    cache_drop(rcache);
    cache_drop(pcache);

    // Save alloc state to superblock
    int rc2 = superblock_write(*this, SLIM_SUPER_BLOCK_A);
    if (!rc)
        rc = rc2;
    rc2 = superblock_write(*this, SLIM_SUPER_BLOCK_B);
    if (!rc)
        rc = rc2;

    cfg = nullptr;
    return rc;
}

// =====================================================================
// Public API: stat
// =====================================================================

int SlimFs::stat(const char* path, SlimInfo& info) noexcept {
    FindResult fr;
    int rc = path_resolve(*this, path, fr);
    if (rc)
        return rc;

    info.type = fr.header.type;
    info.size = fr.header.size;

    if (fr.header.name_len > 0) {
        rc = entry_read_name(*this, fr.block, fr.entry_off, fr.header, info.name, sizeof(info.name));
        if (rc)
            return rc;
    } else {
        info.name[0] = '/';
        info.name[1] = '\0';
    }
    return 0;
}

// =====================================================================
// Public API: mkdir
// =====================================================================

int SlimFs::mkdir(const char* path) noexcept {
    uint32_t parent_block;
    const char* name;
    uint32_t name_len;
    int rc = path_resolve_parent(*this, path, parent_block, name, name_len);
    if (rc)
        return rc;

    // Check if already exists
    FindResult fr;
    rc = dir_find_entry(*this, parent_block, name, name_len, fr);
    if (rc == 0)
        return err(SlimError::EXIST);
    if (rc != err(SlimError::NOENT))
        return rc;

    // Allocate a block for the new directory
    uint32_t new_block;
    rc = alloc_block(*this, new_block);
    if (rc)
        return rc;

    // Write empty metadata block for new directory
    rc = meta_write_empty(*this, new_block, 0);
    if (rc)
        return rc;

    // Add entry to parent
    rc = meta_append_entry(*this, parent_block, SlimType::DIR, name, name_len, 0, new_block);
    if (rc)
        return rc;

    cache_drop(rcache);
    return 0;
}

// =====================================================================
// Public API: dir_open / dir_read / dir_close
// =====================================================================

int SlimFs::dir_open(SlimDir& dir, const char* path) noexcept {
    FindResult fr;
    int rc = path_resolve(*this, path, fr);
    if (rc)
        return rc;

    uint32_t dir_block;
    if (fr.header.name_len == 0) {
        // Root
        dir_block = root_block;
    } else {
        if (fr.header.type != SlimType::DIR)
            return err(SlimError::NOTDIR);
        dir_block = fr.header.head_block;
    }

    dir.block = dir_block;
    dir.pos = 0;

    uint32_t revision, next_block, entry_count;
    rc = meta_read_header(*this, dir_block, revision, next_block, entry_count);
    if (rc)
        return rc;
    dir.count = entry_count;
    return 0;
}

int SlimFs::dir_read(SlimDir& dir, SlimInfo& info) noexcept {
    if (dir.block == SLIM_BLOCK_NULL)
        return err(SlimError::BADF);

    uint32_t revision, next_block, entry_count;
    int rc = meta_read_header(*this, dir.block, revision, next_block, entry_count);
    if (rc)
        return rc;

    // Skip to current position
    uint32_t off = SLIM_META_HEADER_SIZE;
    uint32_t idx = 0;
    uint32_t visible = 0;

    while (idx < entry_count) {
        EntryHeader eh;
        rc = entry_read_header(*this, dir.block, off, eh);
        if (rc)
            return rc;

        if (eh.type != SlimType::DEL) {
            if (visible == dir.pos) {
                // Found it
                info.type = eh.type;
                info.size = eh.size;
                rc = entry_read_name(*this, dir.block, off, eh, info.name, sizeof(info.name));
                if (rc)
                    return rc;
                dir.pos = visible + 1;
                return 1;
            }
            ++visible;
        }
        off += entry_total_size(eh);
        ++idx;
    }

    // No more entries
    return 0; // Return 0 with no data to indicate end (like littlefs convention)
}

int SlimFs::dir_seek(SlimDir& dir, uint32_t off) noexcept {
    if (dir.block == SLIM_BLOCK_NULL)
        return err(SlimError::BADF);
    dir.pos = off;
    return 0;
}

int SlimFs::dir_tell(const SlimDir& dir) const noexcept {
    if (dir.block == SLIM_BLOCK_NULL)
        return err(SlimError::BADF);
    return static_cast<int>(dir.pos);
}

int SlimFs::dir_rewind(SlimDir& dir) noexcept {
    if (dir.block == SLIM_BLOCK_NULL)
        return err(SlimError::BADF);
    dir.pos = 0;
    return 0;
}

int SlimFs::dir_close(SlimDir& dir) noexcept {
    dir.block = SLIM_BLOCK_NULL;
    dir.pos = 0;
    dir.count = 0;
    return 0;
}

// =====================================================================
// Public API: file_open / file_close / file_read / file_write / file_seek
// =====================================================================

int SlimFs::file_open(SlimFile& file, const char* path, SlimOpenFlags flags) noexcept {
    file = SlimFile{};
    file.flags = flags;

    uint32_t parent_block;
    const char* name;
    uint32_t name_len;
    int rc = path_resolve_parent(*this, path, parent_block, name, name_len);
    if (rc)
        return rc;

    FindResult fr;
    rc = dir_find_entry(*this, parent_block, name, name_len, fr);

    if (rc == 0) {
        // File exists
        if (has_flag(flags, SlimOpenFlags::EXCL) && has_flag(flags, SlimOpenFlags::CREAT))
            return err(SlimError::EXIST);
        if (fr.header.type == SlimType::DIR)
            return err(SlimError::ISDIR);

        file.size = fr.header.size;
        file.head_block = fr.header.head_block;
        file.is_inline = (fr.header.head_block == SLIM_BLOCK_INLINE);
        file.dir_block = fr.block;
        file.entry_index = static_cast<uint16_t>(fr.entry_index);
        file.cur_block = file.head_block;
        file.cur_off = 0;

        if (has_flag(flags, SlimOpenFlags::TRUNC)) {
            // Free existing data
            if (!file.is_inline && file.head_block != SLIM_BLOCK_NULL) {
                chain_free(*this, file.head_block);
            }
            file.size = 0;
            file.head_block = SLIM_BLOCK_NULL;
            file.cur_block = SLIM_BLOCK_NULL;
            file.dirty = true;
        }

        if (has_flag(flags, SlimOpenFlags::APPEND)) {
            file.pos = file.size;
        }

        return 0;
    }

    if (rc != err(SlimError::NOENT))
        return rc;

    // File doesn't exist
    if (!has_flag(flags, SlimOpenFlags::CREAT))
        return err(SlimError::NOENT);

    // Create new file entry
    rc = meta_append_entry(*this, parent_block, SlimType::REG, name, name_len, 0, SLIM_BLOCK_NULL);
    if (rc)
        return rc;

    cache_drop(rcache);

    // Re-find the entry we just created
    rc = dir_find_entry(*this, parent_block, name, name_len, fr);
    if (rc)
        return rc;

    file.size = 0;
    file.head_block = SLIM_BLOCK_NULL;
    file.is_inline = false;
    file.dir_block = fr.block;
    file.entry_index = static_cast<uint16_t>(fr.entry_index);
    file.cur_block = SLIM_BLOCK_NULL;
    file.cur_off = 0;

    return 0;
}

int SlimFs::file_close(SlimFile& file) noexcept {
    int rc = file_sync(file);
    file = SlimFile{};
    return rc;
}

int SlimFs::file_sync(SlimFile& file) noexcept {
    if (!file.dirty)
        return 0;

    // Update directory entry with current size and head_block
    // Find the entry offset in the directory block
    uint32_t revision, next_blk, entry_count;
    int rc = meta_read_header(*this, file.dir_block, revision, next_blk, entry_count);
    if (rc)
        return rc;

    uint32_t off = SLIM_META_HEADER_SIZE;
    for (uint32_t i = 0; i < entry_count; ++i) {
        EntryHeader eh;
        rc = entry_read_header(*this, file.dir_block, off, eh);
        if (rc)
            return rc;

        if (i == file.entry_index) {
            rc = meta_update_entry(*this, file.dir_block, off, file.size, file.head_block);
            if (rc)
                return rc;
            // COW: free old chain now that metadata points to new chain
            if (file.old_head_block != SLIM_BLOCK_NULL) {
                (void)chain_free(*this, file.old_head_block);
                file.old_head_block = SLIM_BLOCK_NULL;
            }
            file.dirty = false;
            cache_drop(rcache);
            return 0;
        }
        off += entry_total_size(eh);
    }

    return err(SlimError::CORRUPT);
}

int SlimFs::file_read(SlimFile& file, std::span<uint8_t> buf) noexcept {
    if (!has_flag(file.flags, SlimOpenFlags::RDONLY) && !has_flag(file.flags, SlimOpenFlags::RDWR)) {
        return err(SlimError::BADF);
    }

    // COW constraint: reading from a dirty file with pending COW is not supported.
    // The new chain may be incomplete. Sync first or use separate RDONLY handle.
    if (file.dirty && file.old_head_block != SLIM_BLOCK_NULL) {
        return err(SlimError::BADF);
    }

    uint32_t size = static_cast<uint32_t>(buf.size());
    if (file.pos >= file.size)
        return 0;
    if (file.pos + size > file.size)
        size = file.size - file.pos;
    if (size == 0)
        return 0;

    uint32_t data_per_block = chain_data_size(cfg->block_size);
    uint32_t read = 0;

    auto* dst = buf.data();
    uint32_t blk_sz = cfg->block_size;

    while (read < size) {
        // Find current block — cur_off tracks the file offset at the start of cur_block
        if (file.cur_block == SLIM_BLOCK_NULL || file.cur_off > file.pos) {
            uint32_t off_in_block;
            int rc = chain_walk(*this, file.head_block, file.pos, file.cur_block, off_in_block);
            if (rc)
                return rc;
            file.cur_off = file.pos - off_in_block;
        }

        uint32_t block_off = file.pos - file.cur_off;

        // Fast path: full-block read from offset 0 with enough user buffer remaining
        // Read directly into user buffer, extract next pointer from the read data,
        // avoiding the cache fill + memcpy double copy.
        if (block_off == 0 && size - read >= blk_sz) {
            // Read entire block into user buffer (data_per_block + 4B next pointer)
            int rc = bd_read(*this, file.cur_block, 0, dst + read, blk_sz);
            if (rc)
                return rc;
            // Invalidate rcache — it no longer matches any block
            cache_drop(rcache);

            read += data_per_block;
            file.pos += data_per_block;

            // Extract next pointer from the tail of what we just read
            auto* p = dst + read - data_per_block + blk_sz - 4;
            uint32_t next = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            file.cur_block = next;
            file.cur_off += data_per_block;
            continue;
        }

        // Slow path: partial block read via cache
        if (!(rcache.valid() && rcache.block == file.cur_block)) {
            int rc = bd_read(*this, file.cur_block, 0, rcache.buffer.data(), blk_sz);
            if (rc)
                return rc;
            rcache.block = file.cur_block;
            rcache.off = 0;
            rcache.size = blk_sz;
        }

        uint32_t chunk = std::min(data_per_block - block_off, size - read);
        std::memcpy(dst + read, rcache.buffer.data() + block_off, chunk);
        read += chunk;
        file.pos += chunk;

        // Advance to next block if this block is fully consumed
        if (block_off + chunk >= data_per_block && read < size) {
            auto* p = rcache.buffer.data() + (blk_sz - 4);
            uint32_t next = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            file.cur_block = next;
            file.cur_off += data_per_block;
        }
    }

    return static_cast<int>(read);
}

// COW helper: allocate a new block, erase it, and set terminal marker
int cow_alloc_data_block(SlimFs& fs, uint32_t& out) {
    int rc = alloc_block(fs, out);
    if (rc)
        return rc;
    rc = bd_erase(fs, out);
    if (rc)
        return rc;
    return chain_write_next(fs, out, SLIM_BLOCK_NULL);
}

// COW helper: copy region from old block to new block via tmp buffer.
int cow_copy_region(SlimFs& fs, uint32_t old_block, uint32_t new_block, uint32_t from, uint32_t to) {
    uint8_t tmp[64];
    uint32_t pos = from;
    while (pos < to) {
        uint32_t chunk = std::min(to - pos, static_cast<uint32_t>(sizeof(tmp)));
        int rc = cache_read(fs, fs.rcache, old_block, pos, tmp, chunk);
        if (rc != 0) {
            return rc;
        }
        rc = bd_prog(fs, new_block, pos, tmp, chunk);
        if (rc != 0) {
            return rc;
        }
        pos += chunk;
    }
    return 0;
}

// COW helper: prepare a new block for writing at block_idx.
// Copies existing data from old_block (before block_off and after block_off + write_len).
int cow_prepare_block(SlimFs& fs,
                      SlimFile& file,
                      uint32_t block_idx,
                      uint32_t block_off,
                      uint32_t write_len,
                      uint32_t data_per_block,
                      uint32_t& out_block) {
    // Find corresponding old block for COW copy
    uint32_t old_block = SLIM_BLOCK_NULL;
    if (file.old_head_block != SLIM_BLOCK_NULL) {
        old_block = file.old_head_block;
        for (uint32_t bi = 0; bi < block_idx; ++bi) {
            uint32_t next;
            int rc = chain_read_next(fs, old_block, next);
            if (rc != 0 || next == SLIM_BLOCK_NULL) {
                old_block = SLIM_BLOCK_NULL;
                break;
            }
            old_block = next;
        }
    }

    // Allocate new COW block
    int rc = cow_alloc_data_block(fs, out_block);
    if (rc != 0) {
        return rc;
    }

    if (old_block != SLIM_BLOCK_NULL) {
        // Copy data before write region
        if (block_off > 0) {
            rc = cow_copy_region(fs, old_block, out_block, 0, block_off);
            if (rc != 0) {
                return rc;
            }
        }
        // Copy data after write region
        uint32_t after = block_off + write_len;
        if (after < data_per_block) {
            rc = cow_copy_region(fs, old_block, out_block, after, data_per_block);
            if (rc != 0) {
                return rc;
            }
        }
    }

    // Link into new chain
    if (file.head_block == SLIM_BLOCK_NULL) {
        file.head_block = out_block;
    } else {
        rc = chain_write_next(fs, file.cur_block, out_block);
        if (rc != 0) {
            return rc;
        }
    }
    file.cur_block = out_block;
    file.cur_off = block_idx * data_per_block;
    return 0;
}

int SlimFs::file_write(SlimFile& file, std::span<const uint8_t> buf) noexcept {
    if (!has_flag(file.flags, SlimOpenFlags::WRONLY) && !has_flag(file.flags, SlimOpenFlags::RDWR))
        return err(SlimError::BADF);

    uint32_t size = static_cast<uint32_t>(buf.size());
    if (size == 0)
        return 0;

    if (file.pos + size > static_cast<uint32_t>(file_max))
        return err(SlimError::FBIG);

    // On first write after open, save old chain for COW
    if (!file.dirty && file.head_block != SLIM_BLOCK_NULL) {
        file.old_head_block = file.head_block;
        file.head_block = SLIM_BLOCK_NULL;
        file.cur_block = SLIM_BLOCK_NULL;
    }

    uint32_t data_per_block = chain_data_size(cfg->block_size);
    uint32_t written = 0;

    while (written < size) {
        uint32_t block_idx = file.pos / data_per_block;
        uint32_t block_off = file.pos % data_per_block;
        uint32_t avail = data_per_block - block_off;
        uint32_t chunk = std::min(avail, size - written);

        // Reuse current COW block if writing within the same block
        bool reuse_cur = file.dirty && file.cur_block != SLIM_BLOCK_NULL && file.cur_off / data_per_block == block_idx;

        uint32_t target_block;
        if (reuse_cur) {
            target_block = file.cur_block;
        } else {
            int rc = cow_prepare_block(*this, file, block_idx, block_off, chunk, data_per_block, target_block);
            if (rc != 0) {
                // Error recovery: free partial new chain, restore old head
                if (file.head_block != SLIM_BLOCK_NULL && file.old_head_block != SLIM_BLOCK_NULL) {
                    (void)chain_free(*this, file.head_block);
                    file.head_block = file.old_head_block;
                    file.old_head_block = SLIM_BLOCK_NULL;
                    file.cur_block = SLIM_BLOCK_NULL;
                    file.dirty = false;
                }
                return rc;
            }
        }

        int rc = bd_prog(*this, target_block, block_off, buf.data() + written, chunk);
        if (rc != 0) {
            return rc;
        }

        written += chunk;
        file.pos += chunk;
        if (file.pos > file.size) {
            file.size = file.pos;
        }
        file.dirty = true;
    }

    return static_cast<int>(written);
}

int SlimFs::file_seek(SlimFile& file, int32_t off, SlimWhence whence) noexcept {
    int32_t new_pos;
    switch (whence) {
    case SlimWhence::SET:
        new_pos = off;
        break;
    case SlimWhence::CUR:
        new_pos = static_cast<int32_t>(file.pos) + off;
        break;
    case SlimWhence::END:
        new_pos = static_cast<int32_t>(file.size) + off;
        break;
    default:
        return err(SlimError::INVAL);
    }
    if (new_pos < 0)
        return err(SlimError::INVAL);
    file.pos = static_cast<uint32_t>(new_pos);
    // Reset block cursor
    file.cur_block = SLIM_BLOCK_NULL;
    return static_cast<int>(file.pos);
}

int SlimFs::file_truncate(SlimFile& file, uint32_t size) noexcept {
    if (size >= file.size) {
        return 0; // Only shrink supported
    }

    // Flush pending COW writes before truncating
    if (file.dirty) {
        int rc = file_sync(file);
        if (rc != 0) {
            return rc;
        }
    }

    if (size == 0) {
        // Free all blocks
        if (!file.is_inline && file.head_block != SLIM_BLOCK_NULL) {
            int rc = chain_free(*this, file.head_block);
            if (rc)
                return rc;
        }
        file.head_block = SLIM_BLOCK_NULL;
        file.size = 0;
        file.pos = 0;
        file.cur_block = SLIM_BLOCK_NULL;
        file.dirty = true;
        return 0;
    }

    // Walk to the block containing the new end, free the rest
    uint32_t data_per_block = chain_data_size(cfg->block_size);
    uint32_t keep_blocks = (size + data_per_block - 1) / data_per_block;
    uint32_t cur = file.head_block;

    for (uint32_t i = 1; i < keep_blocks; ++i) {
        uint32_t next;
        int rc = chain_read_next(*this, cur, next);
        if (rc)
            return rc;
        if (next == SLIM_BLOCK_NULL)
            break;
        cur = next;
    }

    // Free remaining chain
    uint32_t next;
    int rc = chain_read_next(*this, cur, next);
    if (rc)
        return rc;
    if (next != SLIM_BLOCK_NULL) {
        rc = chain_free(*this, next);
        if (rc)
            return rc;
    }

    // Set terminal on last kept block
    rc = chain_write_next(*this, cur, SLIM_BLOCK_NULL);
    if (rc)
        return rc;

    file.size = size;
    if (file.pos > size)
        file.pos = size;
    file.cur_block = SLIM_BLOCK_NULL;
    file.dirty = true;
    return 0;
}

int SlimFs::file_tell(const SlimFile& file) const noexcept {
    return static_cast<int>(file.pos);
}

int SlimFs::file_size(const SlimFile& file) const noexcept {
    return static_cast<int>(file.size);
}

// =====================================================================
// Public API: remove
// =====================================================================

int SlimFs::remove(const char* path) noexcept {
    uint32_t parent_block;
    const char* name;
    uint32_t name_len;
    int rc = path_resolve_parent(*this, path, parent_block, name, name_len);
    if (rc)
        return rc;

    FindResult fr;
    rc = dir_find_entry(*this, parent_block, name, name_len, fr);
    if (rc)
        return rc;

    // If directory, check empty
    if (fr.header.type == SlimType::DIR) {
        uint32_t revision, next_block, entry_count;
        rc = meta_read_header(*this, fr.header.head_block, revision, next_block, entry_count);
        if (rc)
            return rc;

        // Check for non-deleted entries
        uint32_t off = SLIM_META_HEADER_SIZE;
        for (uint32_t i = 0; i < entry_count; ++i) {
            EntryHeader eh;
            rc = entry_read_header(*this, fr.header.head_block, off, eh);
            if (rc)
                return rc;
            if (eh.type != SlimType::DEL)
                return err(SlimError::NOTEMPTY);
            off += entry_total_size(eh);
        }
    }

    // Free file data if applicable
    if (fr.header.type == SlimType::REG && fr.header.head_block != SLIM_BLOCK_NULL &&
        fr.header.head_block != SLIM_BLOCK_INLINE) {
        rc = chain_free(*this, fr.header.head_block);
        if (rc)
            return rc;
    }

    // Mark entry as deleted
    rc = meta_delete_entry(*this, fr.block, fr.entry_off);
    if (rc)
        return rc;

    cache_drop(rcache);
    return 0;
}

// =====================================================================
// Public API: rename
// =====================================================================

int SlimFs::rename(const char* oldpath, const char* newpath) noexcept {
    // Find source
    uint32_t src_parent;
    const char* src_name;
    uint32_t src_name_len;
    int rc = path_resolve_parent(*this, oldpath, src_parent, src_name, src_name_len);
    if (rc)
        return rc;

    FindResult src_fr;
    rc = dir_find_entry(*this, src_parent, src_name, src_name_len, src_fr);
    if (rc)
        return rc;

    // Find dest parent
    uint32_t dst_parent;
    const char* dst_name;
    uint32_t dst_name_len;
    rc = path_resolve_parent(*this, newpath, dst_parent, dst_name, dst_name_len);
    if (rc)
        return rc;

    // Check if dest already exists
    FindResult dst_fr;
    rc = dir_find_entry(*this, dst_parent, dst_name, dst_name_len, dst_fr);
    if (rc == 0) {
        // Dest exists — remove it first
        if (dst_fr.header.type == SlimType::DIR) {
            // Check empty
            uint32_t rev, nb, ec;
            rc = meta_read_header(*this, dst_fr.header.head_block, rev, nb, ec);
            if (rc)
                return rc;
            uint32_t off = SLIM_META_HEADER_SIZE;
            for (uint32_t i = 0; i < ec; ++i) {
                EntryHeader eh;
                rc = entry_read_header(*this, dst_fr.header.head_block, off, eh);
                if (rc)
                    return rc;
                if (eh.type != SlimType::DEL)
                    return err(SlimError::NOTEMPTY);
                off += entry_total_size(eh);
            }
        }
        // Free dest data
        if (dst_fr.header.type == SlimType::REG && dst_fr.header.head_block != SLIM_BLOCK_NULL &&
            dst_fr.header.head_block != SLIM_BLOCK_INLINE) {
            chain_free(*this, dst_fr.header.head_block);
        }
        rc = meta_delete_entry(*this, dst_fr.block, dst_fr.entry_off);
        if (rc)
            return rc;
    } else if (rc != err(SlimError::NOENT)) {
        return rc;
    }

    // Determine dst entry index (will be entry_count after append)
    {
        uint32_t rev, nb, ec;
        rc = meta_read_header(*this, dst_parent, rev, nb, ec);
        if (rc)
            return rc;
        pending_move.dst_id = static_cast<uint16_t>(ec);
    }

    // Record pending_move in superblock (crash safety)
    pending_move.src_dir = src_parent;
    pending_move.src_id = static_cast<uint16_t>(src_fr.entry_index);
    pending_move.dst_dir = dst_parent;
    rc = superblock_write(*this, SLIM_SUPER_BLOCK_A);
    if (rc)
        return rc;

    // Add entry to dest
    rc = meta_append_entry(
        *this, dst_parent, src_fr.header.type, dst_name, dst_name_len, src_fr.header.size, src_fr.header.head_block);
    if (rc)
        return rc;

    // Delete entry from source
    rc = meta_delete_entry(*this, src_fr.block, src_fr.entry_off);
    if (rc)
        return rc;

    // Clear pending_move
    pending_move.clear();
    rc = superblock_write(*this, SLIM_SUPER_BLOCK_A);
    if (rc)
        return rc;

    cache_drop(rcache);
    return 0;
}

// =====================================================================
// Public API: fs_size
// =====================================================================

int SlimFs::fs_size() noexcept {
    // Count used blocks
    int rc = alloc_full_scan(*this);
    if (rc)
        return rc;

    uint32_t used = 0;
    for (uint32_t i = 0; i < lookahead.size; ++i) {
        if (lookahead.buffer[i / 8] & (1u << (i % 8)))
            ++used;
    }
    return static_cast<int>(used);
}

// =====================================================================
// Public API: fs_traverse
// =====================================================================

int SlimFs::fs_traverse(int (*cb)(void*, uint32_t), void* data) noexcept {
    if (!cb)
        return err(SlimError::INVAL);

    int rc = alloc_full_scan(*this);
    if (rc)
        return rc;

    for (uint32_t i = 0; i < lookahead.size; ++i) {
        if (lookahead.buffer[i / 8] & (1u << (i % 8))) {
            uint32_t block = (lookahead.start + i) % block_count;
            rc = cb(data, block);
            if (rc)
                return rc;
        }
    }
    return 0;
}

// =====================================================================
// Public API: block_size / block_count_total
// =====================================================================

uint32_t SlimFs::block_size() const noexcept {
    return cfg ? cfg->block_size : 0;
}

uint32_t SlimFs::block_count_total() const noexcept {
    return block_count;
}

// =====================================================================
// Public API: custom attributes (getattr / setattr / removeattr)
// =====================================================================

// Attribute TLV format within the entry's attr region:
//   [type: 1][len: 1][data: len]  (repeating)
// Total attr_size in entry header = sum of all (2 + len) for each attr.

int SlimFs::getattr(const char* path, uint8_t type, std::span<uint8_t> buf) noexcept {
    FindResult fr;
    int rc = path_resolve(*this, path, fr);
    if (rc)
        return rc;

    if (fr.header.attr_size == 0)
        return err(SlimError::NOENT);

    // Read attr blob
    uint32_t attr_off = fr.entry_off + SLIM_ENTRY_HEADER_SIZE + fr.header.name_len;
    uint32_t remaining = fr.header.attr_size;
    uint32_t pos = 0;

    while (pos + 2 <= remaining) {
        uint8_t tlv[2];
        rc = cache_read(*this, rcache, fr.block, attr_off + pos, tlv, 2);
        if (rc)
            return rc;
        uint8_t attr_type = tlv[0];
        uint8_t attr_len = tlv[1];
        if (pos + 2 + attr_len > remaining)
            return err(SlimError::CORRUPT);
        if (attr_type == type) {
            uint32_t copy_len = attr_len;
            if (copy_len > buf.size())
                copy_len = static_cast<uint32_t>(buf.size());
            if (copy_len > 0) {
                rc = cache_read(*this, rcache, fr.block, attr_off + pos + 2, buf.data(), copy_len);
                if (rc)
                    return rc;
            }
            return static_cast<int>(attr_len);
        }
        pos += 2 + attr_len;
    }

    return err(SlimError::NOENT);
}

// Rewrite a metadata block, replacing the attr blob for one entry.
// This is needed because changing attr_size shifts all subsequent entries.
namespace {
int meta_rewrite_attrs(SlimFs& fs, uint32_t block, uint32_t target_entry_off,
                       const uint8_t* new_attrs, uint32_t new_attr_size,
                       uint32_t old_attr_size) {
    uint32_t total;
    int rc = meta_used_size(fs, block, total);
    if (rc)
        return rc;
    uint32_t data_end = total - 4;

    // Size change
    int32_t delta = static_cast<int32_t>(new_attr_size) - static_cast<int32_t>(old_attr_size);
    uint32_t new_data_end = static_cast<uint32_t>(static_cast<int32_t>(data_end) + delta);
    if (new_data_end + 4 > fs.cfg->block_size)
        return err(SlimError::NOSPC);

    uint8_t* buf = fs.pcache.buffer.data();
    rc = bd_read(fs, block, 0, buf, data_end);
    if (rc)
        return rc;

    // Find where the old attrs start and end for target entry
    uint8_t name_len = buf[target_entry_off + 1];
    uint32_t attr_start = target_entry_off + SLIM_ENTRY_HEADER_SIZE + name_len;
    uint32_t old_entry_end = attr_start + old_attr_size;

    // Shift data after old attrs
    if (delta != 0 && old_entry_end < data_end) {
        std::memmove(buf + attr_start + new_attr_size, buf + old_entry_end, data_end - old_entry_end);
    }

    // Write new attrs
    if (new_attr_size > 0) {
        std::memcpy(buf + attr_start, new_attrs, new_attr_size);
    }

    // Update attr_size in entry header
    st_u32(buf + target_entry_off + 12, new_attr_size);

    // Increment revision
    uint32_t rev = ld_u32(buf);
    st_u32(buf, rev + 1);

    cache_drop(fs.rcache);

    rc = bd_erase(fs, block);
    if (rc)
        return rc;
    rc = bd_prog(fs, block, 0, buf, new_data_end);
    if (rc)
        return rc;

    uint32_t crc_val = crc32(0xFFFFFFFF, buf, new_data_end);
    uint8_t crc_buf[4];
    st_u32(crc_buf, crc_val);
    rc = bd_prog(fs, block, new_data_end, crc_buf, 4);
    if (rc)
        return rc;

    return bd_sync(fs);
}
} // namespace

int SlimFs::setattr(const char* path, uint8_t type, std::span<const uint8_t> buf) noexcept {
    if (buf.size() > 255)
        return err(SlimError::INVAL);

    FindResult fr;
    int rc = path_resolve(*this, path, fr);
    if (rc)
        return rc;

    // Read existing attrs
    uint8_t old_attrs[512];
    uint32_t old_attr_size = fr.header.attr_size;
    if (old_attr_size > sizeof(old_attrs))
        return err(SlimError::NOSPC);
    if (old_attr_size > 0) {
        uint32_t attr_off = fr.entry_off + SLIM_ENTRY_HEADER_SIZE + fr.header.name_len;
        rc = cache_read(*this, rcache, fr.block, attr_off, old_attrs, old_attr_size);
        if (rc)
            return rc;
    }

    // Build new attr blob: copy all except matching type, then append new
    uint8_t new_attrs[512];
    uint32_t new_size = 0;
    uint32_t pos = 0;
    while (pos + 2 <= old_attr_size) {
        uint8_t attr_type = old_attrs[pos];
        uint8_t attr_len = old_attrs[pos + 1];
        if (pos + 2 + attr_len > old_attr_size)
            break;
        if (attr_type != type) {
            if (new_size + 2 + attr_len > sizeof(new_attrs))
                return err(SlimError::NOSPC);
            std::memcpy(new_attrs + new_size, old_attrs + pos, 2 + attr_len);
            new_size += 2 + attr_len;
        }
        pos += 2 + attr_len;
    }

    // Append new attr
    if (new_size + 2 + buf.size() > sizeof(new_attrs))
        return err(SlimError::NOSPC);
    new_attrs[new_size] = type;
    new_attrs[new_size + 1] = static_cast<uint8_t>(buf.size());
    if (!buf.empty())
        std::memcpy(new_attrs + new_size + 2, buf.data(), buf.size());
    new_size += 2 + static_cast<uint32_t>(buf.size());

    return meta_rewrite_attrs(*this, fr.block, fr.entry_off, new_attrs, new_size, old_attr_size);
}

int SlimFs::removeattr(const char* path, uint8_t type) noexcept {
    FindResult fr;
    int rc = path_resolve(*this, path, fr);
    if (rc)
        return rc;

    if (fr.header.attr_size == 0)
        return err(SlimError::NOENT);

    uint8_t old_attrs[512];
    uint32_t old_attr_size = fr.header.attr_size;
    if (old_attr_size > sizeof(old_attrs))
        return err(SlimError::NOSPC);
    uint32_t attr_off = fr.entry_off + SLIM_ENTRY_HEADER_SIZE + fr.header.name_len;
    rc = cache_read(*this, rcache, fr.block, attr_off, old_attrs, old_attr_size);
    if (rc)
        return rc;

    // Rebuild without matching type
    uint8_t new_attrs[512];
    uint32_t new_size = 0;
    bool found = false;
    uint32_t pos = 0;
    while (pos + 2 <= old_attr_size) {
        uint8_t attr_type = old_attrs[pos];
        uint8_t attr_len = old_attrs[pos + 1];
        if (pos + 2 + attr_len > old_attr_size)
            break;
        if (attr_type == type) {
            found = true;
        } else {
            std::memcpy(new_attrs + new_size, old_attrs + pos, 2 + attr_len);
            new_size += 2 + attr_len;
        }
        pos += 2 + attr_len;
    }

    if (!found)
        return err(SlimError::NOENT);

    return meta_rewrite_attrs(*this, fr.block, fr.entry_off, new_attrs, new_size, old_attr_size);
}

// =====================================================================
// Public API: fs_gc (metadata compaction)
// =====================================================================

int SlimFs::fs_gc() noexcept {
    // Compact root directory and all subdirectories by removing deleted entries
    // Process a single directory block: read all entries, rewrite without DEL entries
    struct CompactState {
        SlimFs* fs;
        int rc;
    };

    // We'll compact the root and discover subdirectories as we go
    // Use a simple stack-based approach with bounded depth
    uint32_t dir_stack[16];
    uint32_t stack_depth = 0;
    dir_stack[stack_depth++] = root_block;

    while (stack_depth > 0) {
        uint32_t dir_block = dir_stack[--stack_depth];

        uint32_t revision, next_block, entry_count;
        int rc = meta_read_header(*this, dir_block, revision, next_block, entry_count);
        if (rc)
            return rc;

        // Check if compaction is needed (any DEL entries?)
        bool has_deleted = false;
        uint32_t off = SLIM_META_HEADER_SIZE;
        for (uint32_t i = 0; i < entry_count; ++i) {
            EntryHeader eh;
            rc = entry_read_header(*this, dir_block, off, eh);
            if (rc)
                return rc;
            if (eh.type == SlimType::DEL)
                has_deleted = true;
            else if (eh.type == SlimType::DIR && stack_depth < 16)
                dir_stack[stack_depth++] = eh.head_block;
            off += entry_total_size(eh);
        }

        if (!has_deleted)
            continue;

        // Read entire block, rebuild without deleted entries
        uint32_t total;
        rc = meta_used_size(*this, dir_block, total);
        if (rc)
            return rc;
        uint32_t data_end = total - 4;

        uint8_t* buf = pcache.buffer.data();
        rc = bd_read(*this, dir_block, 0, buf, data_end);
        if (rc)
            return rc;

        // Build compacted block in-place
        uint32_t src = SLIM_META_HEADER_SIZE;
        uint32_t dst = SLIM_META_HEADER_SIZE;
        uint32_t new_count = 0;

        for (uint32_t i = 0; i < entry_count; ++i) {
            auto type = static_cast<SlimType>(buf[src]);
            uint8_t name_len = buf[src + 1];
            uint32_t attr_size = ld_u32(buf + src + 12);
            uint32_t esz = SLIM_ENTRY_HEADER_SIZE + name_len + attr_size;

            if (type != SlimType::DEL) {
                if (dst != src)
                    std::memmove(buf + dst, buf + src, esz);
                dst += esz;
                ++new_count;
            }
            src += esz;
        }

        // Update header
        st_u32(buf, revision + 1);
        st_u32(buf + 8, new_count);

        cache_drop(rcache);

        rc = bd_erase(*this, dir_block);
        if (rc)
            return rc;
        rc = bd_prog(*this, dir_block, 0, buf, dst);
        if (rc)
            return rc;

        uint32_t crc_val = crc32(0xFFFFFFFF, buf, dst);
        uint8_t crc_buf[4];
        st_u32(crc_buf, crc_val);
        rc = bd_prog(*this, dir_block, dst, crc_buf, 4);
        if (rc)
            return rc;

        rc = bd_sync(*this);
        if (rc)
            return rc;
    }

    return 0;
}

// =====================================================================
// Public API: fs_grow
// =====================================================================

int SlimFs::fs_grow(uint32_t new_block_count) noexcept {
    if (new_block_count < block_count)
        return err(SlimError::INVAL);
    if (new_block_count == block_count)
        return 0;

    block_count = new_block_count;

    // Update lookahead size if it was tracking fewer blocks
    auto& la = lookahead;
    uint32_t la_bits = static_cast<uint32_t>(la.buffer.size()) * 8;
    if (la.size < la_bits && la.size < block_count) {
        la.size = std::min(la_bits, block_count);
    }

    // Persist to superblock
    int rc = superblock_write(*this, SLIM_SUPER_BLOCK_A);
    if (rc)
        return rc;
    return superblock_write(*this, SLIM_SUPER_BLOCK_B);
}

// =====================================================================
// Public API: close_all
// =====================================================================

void SlimFs::close_all(std::span<SlimFile> files) noexcept {
    for (auto& f : files) {
        if (f.head_block != SLIM_BLOCK_NULL || f.dirty) {
            (void)file_sync(f);
            f = SlimFile{};
        }
    }
}

} // namespace umi::fs
