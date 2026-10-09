/**
 * @file provisioning.cpp
 * @brief Node provisioning pulls in the NVS record-store adapter.
 *
 * The composition root in src/node/main.cpp calls PreferencesRecordStore::open().
 */

#include "provisioning.h"

#include <preferences_record_store.h>
