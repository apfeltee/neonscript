#!/usr/bin/ruby

def runtimed(cmd)
  starting = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  system(*cmd)
  ending = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  elapsed = ending - starting
  return elapsed
end

begin
  dt = {}
  begin
    Dir.glob("eg/*.nn") do |file|
      seconds = runtimed(["valgrind", "./run", file])
      dt[file] = seconds 
    end
  ensure
    File.open("tresults.txt", "wb") do |ofh|
      dt.sort_by{|file, seconds| seconds}.each do |file, seconds|
        str = sprintf("%g\t%p\n", seconds, file)
        ofh.print(str)
        $stdout.print(str)
      end
    end
  end
end
