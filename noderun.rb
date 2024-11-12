#!/usr/bin/ruby


begin
  args = []
  preargs = []
  usevg = false
  ARGV.each do |arg|
    if (arg[0] == '-') && ((arg == '-v') || (arg == '--valgrind')) then
      usevg = true
    else
      args.push(arg)
    end
  end
  thisdir = __dir__
  shimjs = File.join(thisdir, "/etc/nodeshim.js")
  #exec valgrind node "$thisdir/etc/nodeshim.js" "${args[@]}"
  cmd = []
  if usevg then
    cmd.push("valgrind")
  end
  cmd.push("node", *preargs, shimjs, *args)
  $stderr.printf("running: %p\n", cmd)
  exec(*cmd)
end