
#pragma once
#include <kernel/kobject/KDriverObject.hpp>

void KE_PNP_Init();
void KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver);
