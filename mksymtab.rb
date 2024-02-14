#!/usr/bin/ruby

require "ostruct"
require "optparse"

class MkSymTable
  def initialize(opts)
    @opts = opts
  end

  def isexcluded(w)
    @opts.excludepats.each do |rx|
      if w.match?(rx) then
        return true
      end
    end
    return false
  end

  def run(file)
    cmd = ["cproto", "-si", "-P", '/* int */ f // (a, b)', file]
    raw = IO.popen(cmd, "rb"){|io| io.read}
    raw.each_line do |line|
      line.strip!
      #$stderr.printf("raw line: %p\n", line)
      next if line.match?(/__inline/)
      line = line.gsub(/\/\/.*$/, "")
      line = line.gsub(/\/\*(\*(?!\/)|[^*])*\*\//, "")
      word = line.stripor
      next if word.empty?
      if not isexcluded(word) then
        yield word
      end
    end
  end
end

begin
  opts = OpenStruct.new({
    excludepats: [],
    asregex: false,
    asgsub: false,
  })
  OptionParser.new{|prs|
    prs.on("-x<pat>", "exclude symbols matching <pat>"){|v|
      opts.excludepats.push(Regexp.compile(v))
    }
    prs.on("-r", "output as regex"){
      opts.asregex = true
    }
    prs.on("-g", "output as gsub-compatible arguments"){
      opts.asgsub = true
    }
  }.parse!
  file = ARGV[0]
  tmp = []
  mst = MkSymTable.new(opts)
  mst.run(file) do |w|
    if opts.asregex then
      tmp.push(w)
    else
      if opts.asgsub then
        printf("  %s=%s \\\n", w, w)
      else
        printf("    {%p => %p},\n", w, w)
      end
    end
  end
  if opts.asregex then
    print('\b(' + tmp.join("|") + ')\b')
  end
end



