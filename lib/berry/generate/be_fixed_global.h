#include "be_constobj.h"

static be_define_const_map_slots(m_libglobal_map) {
    { be_const_key(undef, -1), be_const_func(m_undef) },
    { be_const_key(setmember, 3), be_const_func(m_setglobal) },
    { be_const_key(contains, -1), be_const_func(m_contains) },
    { be_const_key(member, 4), be_const_func(m_findglobal) },
    { be_const_key(_X28_X29, -1), be_const_func(m_globals) },
};

static be_define_const_map(
    m_libglobal_map,
    5
);

static be_define_const_module(
    m_libglobal,
    "global"
);

BE_EXPORT_VARIABLE be_define_const_native_module(global);
