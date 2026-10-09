/**
 * @file storage.cpp
 * @brief Gateway storage pulls in the NVS record-store adapter.
 *
 * The composition root in gateway_main.cpp calls PreferencesRecordStore::open().
 */

#include "storage.h"

#include <preferences_record_store.h>
