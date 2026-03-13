

function main()
{
    left_edge   = -420
    right_edge  =  300
    top_edge    =  300
    bottom_edge = -300
    x_step      =    7
    y_step      =   15
    max_iter    =  100
    y0 = top_edge
    while(y0 > bottom_edge)
    {
        x0 = left_edge
        while(x0 < right_edge)
        {
            y = 0
            x = 0
            chcode = 32
            i = 0
            while (i < max_iter)
            {
                x_x = (x * x) / 200
                y_y = (y * y) / 200
                if (x_x + y_y > 800)
                {
                    chcode = 48 + i
                    if(i > 9)
                    {
                        chcode = 64
                    }
                    i = max_iter
                }
                y = x * y / 100 + y0
                x = x_x - y_y + x0
                i++ 
            }
            pch = '';
            if(chcode > 50)
            {
                pch = "+";
            }
            else if(chcode > 40)
            {
                pch = "-";
            }
            else if(chcode > 30)
            {
                pch = "@";
            }
            else
            {
                pch = "%";
            }
            print(pch);
            x0 = x0 + x_step
        }
        print("\n")
        y0 = y0 - y_step
    }
}

main()

