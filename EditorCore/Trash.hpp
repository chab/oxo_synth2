#pragma once

// Moves a file to the Trash (Finder's on mac). Returns false where there is
// none (iOS app containers): the caller deletes instead. Objective-C++:
// define OXO_TRASH_IMPL before including this in exactly one .mm per binary.

#include <filesystem>

namespace oxo {
bool MoveToTrash(const std::filesystem::path &path);
#if defined(_WIN32)
// The Recycle Bin takes items from `path`'s drive (Windows_Build/Source/WindowsTrash.cpp).
bool CanRecycle(const std::filesystem::path &path);
#endif
}

#ifdef OXO_TRASH_IMPL
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>

bool oxo::MoveToTrash(const std::filesystem::path &path) {
#if TARGET_OS_IPHONE
    (void)path;
    return false; // a container Trash nobody can empty: the caller deletes
#else
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        return [NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:nil error:nil];
    }
#endif
}
#endif
