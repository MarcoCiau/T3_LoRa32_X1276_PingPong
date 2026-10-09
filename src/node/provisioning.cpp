/**
 * @file provisioning.cpp
 * @brief Node provisioning pulls in the NVS record-store adapter.
 *
 * The composition root calls PreferencesRecordStore::open() in a later task.
 */

#include "provisioning.h"

#include <preferences_record_store.h>
