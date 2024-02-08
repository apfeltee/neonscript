#!/usr/bin/ruby

$sources = {}

def initsources()
  ["main.cpp", "optparse.h", "os.h", "strbuf.h", "utf8.h"].each do |file|
    $sources[file] = File.read(file)
  end
end

def insources(name)
  ns = name.scrub
  $sources.each do |file, src|
    if src.match?(/\b#{ns}\b/) then
      return true
    end
  end
  return false
end

begin
  initsources()
  File.foreach("prot.inc") do |line|
    m = line.match(/\b(?<n>\w+)\b\s*\(/)
    next unless m
    n = m["n"]
    if insources(n) then
      print(line)
    end
  end
end

