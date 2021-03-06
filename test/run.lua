#!/usr/bin/env lua
package.cpath = "build/lib/lua/?.so;"..package.cpath
package.path  = "src/?.lua;test/?.lua;"..package.path
luaunit = require ('luaunit')

local tests = {
    'test_bytes',
    'test_midi',
    'test_object',
    'TestAudioBuffer',
    'TestBounds',
    'TestMidiBuffer',
    'TestMidiMessage',
    'TestPoint'
}
for _,t in ipairs (tests) do 
    require (t)
end

local exit_code = luaunit.LuaUnit.run (...)
collectgarbage()
os.exit (exit_code)
