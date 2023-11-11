





begin
  ops = %w(
    NEON_OP_INDEXGET
    NEON_OP_GLOBALGET
    NEON_OP_GLOBALDEFINE
    NEON_OP_GLOBALSET
    NEON_OP_PUSHCONST
    NEON_OP_LOCALSET
    NEON_OP_LOCALGET
    NEON_OP_UPVALSET
    NEON_OP_UPVALGET
    NEON_OP_PROPERTYGET
    NEON_OP_PROPERTYSET
  )

  ranges = [0, 1, 2, 3]

  cnt = ops.length

  rawdata = File.read("codeargs.h.tpl")
  cock = rawdata + ""

  i = 0


  tree = []

  ops.each do |op|

    ranges.each do |r|

      if tree[r] == nil then
        tree[r] = []
      end

      tree[r].push([op, r])

    end



  end




end



