#!/usr/bin/ruby

begin
  args = []
  preargs = []
  ARGV.each do |arg|
    if arg[0] == '-' then
      preargs.push(arg)
    else
      args.push(arg)
    end
  end
  thisdir = __dir__
  shimjs = File.join(thisdir, "/etc/nodeshim.js")
  #exec valgrind node "$thisdir/etc/nodeshim.js" "${args[@]}"
  cmd = ["node", *preargs, shimjs, *args]
  $stderr.printf("running: %p\n", cmd)
  exec(*cmd)
end