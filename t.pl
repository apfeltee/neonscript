


sub nn_vmutil_floordiv
{
    d = (int(a) / int(b));
    return (d - (int(((d * b) == a)) & ((a < 0) ^ (b < 0))));
}
