// Copyright (c) 2026 Stefan Grosser

#pragma once
#include <atomic>

///
/// Appearance settings shared by the provider and its plugins.
///
/// Results of different sources look alike in the flat result list, e.g. an application, its
/// windows, tabs and history entries. These options make the source visible.
///
struct Appearance
{
    std::atomic<bool> source_subtext{true};  ///< Prefix the subtext with the source
    std::atomic<bool> source_badge{true};    ///< Add a badge of the source to the icon
};
