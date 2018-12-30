--- Crow standard library

_crow = {}

--- System functions

_crow.version = '0.0.0'

function whatversion()
    return _crow.version
end

function printversion()
    print( 'crow ' .. whatversion() )
end


--- Library loader
--
local function closelibs()
    -- set whole list of libs to nil to close them
    -- TODO does this free the RAM used by 'dofile'?
    Input  = nil
    Output = nil
    Asl    = nil
    Asllib = nil
    Metro  = nil
end

function _crow.libs( lib )
    if lib == nil then
        -- load all
        Input  = dofile('lua/input.lua')
        Output = dofile('lua/output.lua')
        Asl    = dofile('lua/asl.lua')
        Asllib = dofile('lua/asllib.lua')
        Metro  = dofile('lua/metro.lua')
    elseif type(lib) == 'table' then
        -- load the list 
    else
        if lib == 'close' then closelibs() end
        -- assume string & load single library
    end
end

-- open all libs by default
_crow.libs()



--- Input
input = {1,2}
for chan = 1, #input do
    input[chan] = Input.new( chan )
end

--- Output
out = {1,2,3,4}
for chan = 1, #out do
    out[chan] = Output.new( chan )
end

--- Asl
function toward_handler( id ) end -- do nothing if Asl not active
-- if defined, make sure active before setting up actions and banging
if Asl then
    toward_handler = function( id )
        out[id].asl:step()
    end
end
-- TODO should 'go_toward' be called 'slew'???
-- special wrapper should really be in the ASL lib itself?
function LL_toward( id, d, t, s )
    if type(d) == 'function' then d = d() end
    if type(t) == 'function' then t = t() end
    if type(s) == 'function' then s = s() end
    go_toward( id, d, t, s )
end

function LL_get_state( id )
    return get_state(id)
end



adc_remote = function(chan)
    get_cv(chan)
end


--- Communication functions
-- these will be called from norns (or the REPL)
-- they return values wrapped in strings that can be used in Lua directly
-- via dostring
function get_out( channel ) _crow.tell( 'out_cv', channel, get_state(channel)) end
function get_cv( channel )  _crow.tell( 'ret_cv', channel, io_get_input(channel)) end

function _crow.make_cmd( event_name, ... )
    local out_string = string.format('^^%s(',event_name)
    local args = {...}
    local arg_len = #args
    if arg_len > 0 then
        for i=1,arg_len-1 do
            out_string = out_string .. args[i] .. ',' -- can't use .format bc ?type
        end
        out_string = out_string .. args[arg_len]
    end
    return out_string .. ')'
end

function _crow.tell( event_name, ... )
    print(_crow.make_cmd( event_name, ... ))
end


--- Flash program
function start_flash_chunk()
    -- should kill the currently running lua script
    -- turn off timers & callbacks? else?
    -- call to C to switch from REPL to writer
end





--- Syntax extensions
-- 
-- extend this macro to conditionally take 'once' as first arg
--      if present, fn is only executed on first invocation then reused
function closure_if_table( f )
    local _f = f
    f = function( ... ) -- applies globally
        if type( ... ) == 'table' then
            local args = ...
            return function() return _f( table.unpack(args) ) end
        else
            return _f( ... )
        end
    end
    return f
end
-- these functions are overloaded with the table->closure functionality
-- nb: there is a performance penalty to normal usage due to type()
wrapped_fns = { math.random
              , math.min
              , math.max
              }
for _,fn in ipairs( wrapped_fns ) do
    fn = closure_if_table( fn )
end

print'crowlib loaded'

return _crow
