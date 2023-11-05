function append(arr, thing) {
    arr.push(thing);
}

function len(thing) {
    return thing.length;
}

String.prototype.ord = function()
{
    return this.charCodeAt(0);
}

String.chr = function(n) {
    return String.fromCharCode(n);
}

String.prototype.size = function()
{
    return this.length;
}

Array.prototype.size = function()
{
    return this.length;
}

Array.prototype.clone = function() {
    return JSON.parse(JSON.stringify(this));
}

String.split = function(s, a) {
    return s.split(a);
}

String.indexOf = function(a, b) {
    return a.indexOf(b);
}

String.substr = function(a, ...rest) {
    return a.substring(a, ...rest);
}

Object.append = function(a, b) {
    a.push(b)
}

Map.keys = Object.keys;

function print(...args) {
    //process.stdout.write(...args);
    for (var i = 0; i < args.length; i++) {
        var arg = args[i];
        var sarg = "undefined";
        if (arg != undefined) {
            sarg = arg.toString();
        }
        process.stdout.write(sarg);
    }
}

function printc(...args) {
    //process.stdout.write(...args);
    for (var i = 0; i < args.length; i++) {
        var arg = args[i];
        process.stdout.write(String.fromCharCode(arg));
    }
}


function println(...args) {
    print(...args);
    print("\n");
}

const ARGV = [null]
for (i = 0; i < process.argv.length; i++) {
    ARGV[i + 1] = process.argv[i];
}

// this jank is needed so we don't accidentally override something in the actual target script.
var __noderun_data = {};
__noderun_data.process = process;
__noderun_data.libfs = require("fs");

class File_dummy {
    constructor(f, mode) {
        this.path = f;
    }

    read() {
        return __noderun_data.libfs.readFileSync(this.path).toString();
    }
}

function File(f, m) {
    return new File_dummy(f, m)
}

// [1] is *this* file. would obviously result in an infinite loop.
if (__noderun_data.process.argv.length > 2) {
    __noderun_data.inputfile = __noderun_data.process.argv[2];
    eval(__noderun_data.libfs.readFileSync(__noderun_data.inputfile).toString());
}