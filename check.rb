#!/usr/bin/ruby

require "optparse"

# runs the suiteof *.nn files.
# if the program ends with an error code, abort, etc.
# it's currently a sit-in for a proper test suite.

begin
  usepause = false
  usevalgrind = false
  iscyg = false
  if system("which cygpath >/dev/null") then
    iscyg = true
  end
  thisdir = __dir__;
  OptionParser.new{|prs|
    prs.on("-v"){
      usevalgrind = true
    }
    prs.on("-p"){
      usepause = true
    }
  }.parse!
  if iscyg == true then
    thisdir = IO.popen(["cygpath", "-ma", thisdir]){|io| io.read.strip }
  end
  exe = File.join(thisdir, "run")
  if  ARGV.length > 0 then
    exe = ARGV[0]
  end
  if ! File.file?(exe) then
    $stderr.printf("error: not a file: %p\n", exe)
    exit 1
  end
  if ! File.executable?(exe) then
    $stderr.printf("error: not executable: %p\n", exe)
    exit(1)
  end
  Dir.glob(File.join(thisdir, "*.nn")) do |file|
    if file.match(/bintrees/i) then
      next
    end
    execmd = [exe, file]
    fullcmd = []
    if usevalgrind then
      fullcmd = ["valgrind", *execmd]
    else
      fullcmd = [*execmd]
    end
    print("----------------------------\n")
    printf(">>>>>> RUNNING: %p (%p)\n", file, fullcmd)
    if ! system(*fullcmd) then
      $stderr.printf("**failed** in %p\n", file)
      break
    else
      if usepause then
        $stderr.printf("press ENTER to continue!\n")
        $stdin.gets
      end
    end
  end
end

