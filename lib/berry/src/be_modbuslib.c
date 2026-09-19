#include "berry.h"
#include <stdint.h>
#include <string.h>

static uint32_t register_pair(bvm *vm)
{
    if (be_top(vm) != 2 || !be_isint(vm, 1) || !be_isint(vm, 2) ||
        be_toint(vm, 1) < 0 || be_toint(vm, 1) > 65535 ||
        be_toint(vm, 2) < 0 || be_toint(vm, 2) > 65535)
        be_raise(vm, "value_error", "expected two registers in 0..65535");
    return ((uint32_t)be_toint(vm, 1) << 16) | (uint32_t)be_toint(vm, 2);
}

static int m_float32(bvm *vm)
{
    uint32_t bits = register_pair(vm);
    float value;
    memcpy(&value, &bits, sizeof(value));
    be_pushreal(vm, value);
    be_return(vm);
}

static int m_int32(bvm *vm)
{
    uint32_t bits = register_pair(vm);
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    be_pushint(vm, value);
    be_return(vm);
}

static int m_init(bvm *vm)
{
    static const char source[] =
        "return def (m)\n"
        "  import json\n"
        "  def read(host, address, count, cb, opts, fc)\n"
        "    if opts == nil opts = {} end\n"
        "    var port = opts.find('port', 502)\n"
        "    var unit = opts.find('unit', 1)\n"
        "    if type(host) != 'string' || type(address) != 'int' ||\n"
        "       type(count) != 'int' || type(port) != 'int' || type(unit) != 'int'\n"
        "      raise 'value_error', 'modbus expects a host string and integer address, count, port and unit'\n"
        "    end\n"
        "    var url = 'modbus://' + host + ':' + str(port) + '/' + str(unit) +\n"
        "              '/' + str(fc) + '/' + str(address) + '/' + str(count)\n"
        "    _http_send('GET', url, nil, def (body, status)\n"
        "      if body == nil cb(nil, status == 0 ? -1 : status)\n"
        "      else cb(json.load(body), 0) end\n"
        "    end, nil)\n"
        "  end\n"
        "  m.readHoldingRegisters = def(h, a, n, cb, o) read(h, a, n, cb, o, 3) end\n"
        "  m.readInputRegisters = def(h, a, n, cb, o) read(h, a, n, cb, o, 4) end\n"
        "  m.readCoils = def(h, a, n, cb, o) read(h, a, n, cb, o, 1) end\n"
        "  m.readDiscreteInputs = def(h, a, n, cb, o) read(h, a, n, cb, o, 2) end\n"
        "  m.int16 = def(v) return v >= 32768 ? v - 65536 : v end\n"
        "  return m\n"
        "end\n";
    if (be_loadbuffer(vm, "modbus", source, sizeof(source) - 1) != BE_OK)
        be_raise(vm, "runtime_error", "cannot load modbus");
    be_call(vm, 0);
    be_pushvalue(vm, 1);
    be_call(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
}

static const bntvmodobj_t modbus_attrs[] = {
    be_native_module_function("init", m_init),
    be_native_module_function("float32", m_float32),
    be_native_module_function("int32", m_int32)
};
const bntvmodule_t be_native_module_modbus = {
    .name = "modbus",
    .attrs = modbus_attrs,
    .size = sizeof(modbus_attrs) / sizeof(modbus_attrs[0]),
    .module = NULL
};
