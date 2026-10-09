/**
 * @file storage.cpp
 * @brief Gateway storage pulls in the NVS record-store adapter.
 *
 * The composition root calls PreferencesRecordStore::open() in a later task.
 */

#include "storage.h"

#include <preferences_record_store.h>
