#!/usr/bin/ruby

require "optparse"

# expand '#include' preprocessor lines in a C source file, if
# the file exists in the current dir or parent directory

class Expand
  attr_accessor :globalsearchdirs

  Typ = Struct.new(:stat, :path)

  def initialize(out)
    @out = out
    @totalcnt = 1
    @seenstats = []
    @globalsearchdirs = ["."]
  end

  def find_stat(file)
    searchdirs = @globalsearchdirs.dup
    st = (File.stat(file) rescue nil)
    if st != nil then
      return Typ.new(st, file)
    end
    searchdirs.push(File.dirname(file))
    searchdirs.uniq!
    $stderr.printf("searchdirs for %p: %p\n", file, searchdirs)
    searchdirs.each do |d|
      fp = File.join(d, file)
      st = (File.stat(fp) rescue nil)
      if st != nil then
        return Typ.new(st, fp)
      end
    end
    return nil
  end

  def push_file(file, islocal)
    st = find_stat(file)
    if st == nil then
      $stderr.printf("could not stat %p\n", file)
      if islocal then
        #@out.printf("#include \"%s\"\n", file)
      else
        @out.printf("#include <%s>\n", file)
      end
    else
      stp = st.path
      stdir = File.dirname(file)
      $stderr.printf("stp=%p stdir=%p\n", stp, stdir)
      if !@globalsearchdirs.include?(stdir) then
        @globalsearchdirs.push(stdir)
      end
      # .inc files are special: always include them, even if seen before
      if !@seenstats.include?(stp) || file.match(/\.inc$/i) then
        @seenstats.push(stp)
        parse_file(stp, stdir)
      #else
        #$stderr.printf("already saw %p???\n", file)
      end
    end
  end

  def parse_file(file, fromdir)
    ncnt = 1
    $stderr.printf("reading from %p\n", file)
    #@out.write(sprintf("#ident %p\n", file))
    @out.write(sprintf("#line %d %p\n", ncnt, "<thisfile>/"+file))
    File.foreach(file) do |line|
      ncnt = @totalcnt
      @totalcnt += 1
      m = line.match(/^\s*#\s*include\s*(?<t>[<"])(?<path>.*?)[">]/)
      if m then
        t = m["t"]
        islocal = (t == '"')
        path = m["path"]
        $stderr.printf("t = %p, path = %p\n", t, path)
        push_file(path, islocal)
      else
        #@out.write(sprintf("#line %d %p\n", ncnt, "<thisfile>/"+file))
        @out.write(line)
      end
    end
    @out.write("\n")
  end

  def run
  end
end

begin
  ex = Expand.new($stdout)
  OptionParser.new{|prs|
    prs.on("-I<dir>"){|v|
      ex.globalsearchdirs.push(v)
    }
  }.parse!
  ex.push_file(ARGV[0], true)
  ex.run
end

