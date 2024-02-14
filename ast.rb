
require "rainbow"
require "pry-byebug"
ENV["LIBCLANG"] = "/usr/lib/x86_64-linux-gnu/libclang-17.so.17"
require "ffi/clang"

def oldstuff
  index = FFI::Clang::Index.new
  translation_unit = index.parse_translation_unit("main.c")
  cursor = translation_unit.cursor
  cursor.visit_children do |cursor, parent|
  
    puts("#{cursor.kind} #{cursor.spelling.inspect}")
    binding.pry
    next :recurse 
  end

end


def title(declaration)
  puts ["Symbol:", Rainbow(declaration.spelling).blue.bright, "Type:", Rainbow(declaration.type.spelling).green, declaration.kind.to_s].join(' ')
end

def newstuff
  index = FFI::Clang::Index.new

  # clang -Xclang -ast-dump -fsyntax-only ./examples/docs.cpp

  ARGV.each do |path|
    translation_unit = index.parse_translation_unit(path)
    
    declarations = translation_unit.cursor.select(&:declaration?)
    
    declarations.each do |declaration|
      title declaration
      
      if location = declaration.location
        puts "Defined at #{location.file}:#{location.line}"
      end
      
      if comment = declaration.comment
        # puts Rainbow(comment.inspect).gray
        puts Rainbow(comment.text)
      end
    end
  end
end

newstuff
