

function Tree(value, depth)
{
    var self = {};
    self.depth = depth;
    self.value = value;
    if(depth > 0)
    {
        self.left  = Tree(value - 1, depth - 1);
        self.right = Tree(2 * value + 1, depth - 1);
    }
    else
    {
        self.left  = null;
        self.right = null;
    }
    self.check = function() {
        if(!self.left)
        {
            return self.value;
        }
        return self.value + self.right.check() - self.left.check();
    };
    return self;
}

function main()
{
    var mindepth = 4;
    var maxdepth = mindepth*2;
    var stretchdepth = maxdepth + 1;
    print("mindepth: ", mindepth, " maxdepth: ", maxdepth, ", stretchdepth: ", stretchdepth, "\n");
    print("starting benchmark...\n");
    print("check 1 : ", Tree(0, stretchdepth).check(), "\n");
    var ancient = Tree(0, maxdepth);
    var totaltrees = 1;
    var i = 0;
    while(i < maxdepth) 
    {
        totaltrees = totaltrees * 2;
        i = i + 1;
    }
    var checkcnt = 2;
    i = mindepth;
    while(i < maxdepth)
    {
        var checkval = 0;
        var j = 0;
        while(j < totaltrees)
        {
            var t1 = Tree(j, i);
            var t2 = Tree(-j, i);
            checkval = checkval + t1.check() + t2.check();
            j = j + 1;
        }
        print("number of trees : ", (totaltrees*2), "\n");
        print("Current running depth: ", i, "\n");
        checkcnt = checkcnt + 1;
        var actualval = "(no value)";
        if(checkval == null)
        {
            actualval = checkval;
        }
        print("check ", checkcnt, ": ", actualval, "\n");
        totaltrees = totaltrees / 4;
        i = i + 2;
    }
    print("long lived tree depth : ", maxdepth, "\n");
    print("check ", checkcnt, ": ", ancient.check, "\n");
    print("benchmarking finished\n");
}
main();
