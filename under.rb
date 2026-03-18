#!/usr/bin/ruby

require "ostruct"
require "optparse"

class Underscore
  def initialize(opts)
    @opts = opts
    @cachedata = {}
    @files = []
    @symtab = {}
    if ARGV.length > 0 then
      @files = ARGV
    else
      @files = Dir.glob("*.{h,c}")
    end
  end

  def getdata(file)
    if @cachedata.key?(file) then
      return @cachedata[file]
    end
    data = File.read(file).scrub
    @cachedata[file] = data
    return data
  end

  def cleanup(sym)
    nsym = sym.dup
    nsym = sym.gsub(/_/, "")
    return nsym
  end

  def issomecallable_infile(sym, file)
    d = getdata(file)
    arx = /\b#{sym}\b\s*\(/
    brx = /(->|\.)\b#{sym}\b/
    if d.match?(arx) || d.match?(brx) then
      return true
    end
    return false
  end

  def issomecallable(sym)
    @files.each do |file|
      if issomecallable_infile(sym, file) then
        return true
      end
    end
    return false
  end

  BADPATS = [
    /\b\w+_t\b/,
    /\bsi_\w+\b/,
    /\bva_\w+\b/,
    /jmp_buf/,
  ]

  REPLACEME = {
  }

  def whatwewant(sym)
    BADPATS.each do |bp|
      if sym.match?(bp) then
        return false
      end
    end
    if sym.match?(/\b[A-Z]+(\d*)?_/) || sym.match(/^_/) then
      return false
    end
    if sym.match?(/\w+_\w+/) then
      return !issomecallable(sym)
    end
    return false
  end

  def findinfile(file)
    rx = /\b(?<ident>\w{3,})\b/
    $stderr.printf("now processing %p ...\n", file)
    d = getdata(file)
    d.enum_for(:scan, rx).map{ Regexp.last_match }.each do |m|
      ident = m["ident"]
      if whatwewant(ident) then
        if !@symtab.key?(ident) then
          @symtab[ident] = 0
        end
        @symtab[ident] += 1
      end
    end
  end

  def main
    @files.each do |f|
      findinfile(f)
    end
    if !@opts.doreplace then
      @symtab.each do |sym, cnt|
        printf("  %p => %p,\n", sym, sym.gsub(/_/, ""))
      end
    end
    if @opts.doreplace then
      $stderr.printf("now replacing ...\n")
    end
    @files.each do |file|
      d = File.read(file).scrub
      totalcnt = 0
      @symtab.each do |sym, cnt|
        rep = REPLACEME[sym]
        if rep == nil then
          rep = cleanup(sym)
        end
        $stderr.printf("replacing %p --> %p\n", sym, rep)
        rx = /\b#{sym}\b/
        if d.match?(rx) then
          totalcnt += 1
          d.gsub!(rx, rep)
        end
      end
      if totalcnt > 0 then
        if @opts.doreplace then
          $stderr.printf("replacing %d identifiers in %p\n", totalcnt, file)
          File.write(file, d)
        else
          $stderr.printf("would replace %d identifiers in %p (use '--replace' to actually do it!)\n", totalcnt, file)
        end
      end
    end
  end

end


begin
  opts = OpenStruct.new({
    doreplace: false,
  })
  OptionParser.new{|prs|
    prs.on("-r", "--replace", "do the actual physical replacing"){
      opts.doreplace = true
    }
  }.parse!
  u = Underscore.new(opts)
  u.main
end
