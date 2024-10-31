#!/usr/bin/ruby

require "optparse"

# runs the suite of *.nn files.
# if the program ends with an error code, abort, etc.
# it's currently a sit-in for a proper test suite.
# run with '-v' to run the files with valgrind, i.e.;
# $ ./check.rb -v |& tee out.txt
# $ grep 'errors from' out.txt
# if everything is 0, you're good to go!

begin
  usepause = false
  usevalgrind = false
  iscyg = false
  failfiles = []
  # different drive directory prefixes for cygwin:
  # wsl uses /mnt/ (C: -> /mnt/c/), cygwin uses /cygdrive/ (C: -> /cygdrive/c/)
  # only matters if the exe is somewhere other than in __dir__
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
    # without this, starting the process will fail
    thisdir = IO.popen(["cygpath", "-ma", thisdir]){|io| io.read.strip }
  end
  exe = File.join(thisdir, "run")
  if  ARGV.length > 0 then
    exe = ARGV[0]
    if !exe.include?('/') then
      exe = File.absolute_path(exe)
    end
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
    #if file.match(/bintrees/i) then
      #next
    #end
    execmd = [exe, file]
    fullcmd = []
    # where the magic happens
    if usevalgrind then
      fullcmd = ["valgrind", *execmd]
    else
      fullcmd = [*execmd]
    end
    print("----------------------------\n")
    printf(">>>>>> RUNNING: %p (%p)\n", file, fullcmd)
    if ! system(*fullcmd) then
      $stderr.printf("**failed** in %p\n", file)
      failfiles.push(file)
    else
      if usepause then
        $stderr.printf("press ENTER to continue!\n")
        $stdin.gets
      end
    end
  end
  printf("********************\n")
  printf("finished!\n")
  if failfiles.length > 0 then
    printf("these failed:\n")
    failfiles.each do |file|
      printf(" %s\n", file)
    end
  end
end

