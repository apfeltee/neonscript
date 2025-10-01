#!/usr/bin/ruby

begin
  dt = []
  data = File.read(ARGV[0])
  data.each_line do |line|
    m = line.match(/removing\s+unused\s+section\s+'.(?<section>\w+).(?<name>\w+)'\s+in\s+file/)
    next unless m
    section = m["section"]
    name = m["name"]
    next if dt.include?(name)
    dt.push(name)
  end
  puts(dt)
end
