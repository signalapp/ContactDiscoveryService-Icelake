// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef __CDSI_PROTO_PBUTIL_H
#define __CDSI_PROTO_PBUTIL_H

#include <stdalign.h>
#include "proto/pbtools.h"

// To build a pbtools structure we must allocate space for the heap and for
// the structure itself. Additionally, we may need to add padding to get correct
// alignment - here we simply add the worst case padding size.
#define PBUTIL_WORKSPACE_BASE(type) (sizeof(struct pbtools_heap_t) + (alignof(type) - 1) + sizeof(type))

#endif // __CDSI_PROTO_PBUTIL_H
