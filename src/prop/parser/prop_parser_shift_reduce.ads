package Prop_Parser_Shift_Reduce is

    type Small_Integer is range -32_000 .. 32_000;

    type Shift_Reduce_Entry is record
        T   : Small_Integer;
        Act : Small_Integer;
    end record;
    pragma Pack(Shift_Reduce_Entry);

    subtype Row is Integer range -1 .. Integer'Last;

  --pragma suppress(index_check);

    type Shift_Reduce_Array is array (Row  range <>) of Shift_Reduce_Entry;

    Shift_Reduce_Matrix : constant Shift_Reduce_Array :=
        ( (-1,-1) -- Dummy Entry

-- state  0
,(-1,-1)
-- state  1
,( 0,-3001),( 6, 3),( 11, 2)
,(-1,-3000)
-- state  2
,( 9, 6),(-1,-3000)
-- state  3
,( 9, 7)
,(-1,-3000)
-- state  4
,(-1,-3000)
-- state  5
,(-1,-2)
-- state  6
,( 14, 8)
,(-1,-3000)
-- state  7
,( 14, 8),(-1,-3000)
-- state  8
,(-1,-24)

-- state  9
,( 17, 11),(-1,-3000)
-- state  10
,( 17, 12),(-1,-3000)

-- state  11
,( 10, 13),(-1,-3000)
-- state  12
,( 5, 18),( 7, 22)
,( 12, 19),( 14, 8),( 18, 20),( 19, 21)
,( 21, 16),(-1,-3000)
-- state  13
,( 4, 24),( 14, 8)
,(-1,-3000)
-- state  14
,(-1,-3)
-- state  15
,( 3, 27),( 8, 28)
,( 13, 29),( 15, 30),( 22, 31),(-1,-11)

-- state  16
,( 5, 18),( 7, 22),( 12, 19),( 14, 8)
,( 18, 20),( 19, 21),( 21, 16),(-1,-3000)

-- state  17
,(-1,-13)
-- state  18
,(-1,-14)
-- state  19
,(-1,-15)
-- state  20
,( 5, 18)
,( 7, 22),( 12, 19),( 14, 8),( 18, 20)
,( 19, 21),( 21, 16),(-1,-3000)
-- state  21
,( 5, 18)
,( 7, 22),( 12, 19),( 14, 8),( 18, 20)
,( 19, 21),( 21, 16),(-1,-3000)
-- state  22
,( 5, 18)
,( 7, 22),( 12, 19),( 14, 8),( 18, 20)
,( 19, 21),( 21, 16),(-1,-3000)
-- state  23
,( 16, 36)
,(-1,-3000)
-- state  24
,(-1,-6)
-- state  25
,(-1,-7)
-- state  26
,( 16, 37)
,(-1,-3000)
-- state  27
,( 5, 18),( 7, 22),( 12, 19)
,( 14, 8),( 18, 20),( 19, 21),( 21, 16)
,(-1,-3000)
-- state  28
,( 5, 18),( 7, 22),( 12, 19)
,( 14, 8),( 18, 20),( 19, 21),( 21, 16)
,(-1,-3000)
-- state  29
,( 5, 18),( 7, 22),( 12, 19)
,( 14, 8),( 18, 20),( 19, 21),( 21, 16)
,(-1,-3000)
-- state  30
,( 5, 18),( 7, 22),( 12, 19)
,( 14, 8),( 18, 20),( 19, 21),( 21, 16)
,(-1,-3000)
-- state  31
,( 5, 18),( 7, 22),( 12, 19)
,( 14, 8),( 18, 20),( 19, 21),( 21, 16)
,(-1,-3000)
-- state  32
,( 3, 27),( 8, 28),( 13, 29)
,( 15, 30),( 20, 43),( 22, 31),(-1,-3000)

-- state  33
,(-1,-21)
-- state  34
,(-1,-22)
-- state  35
,(-1,-23)
-- state  36
,(-1,-4)

-- state  37
,(-1,-8)
-- state  38
,( 13, 29),( 15, 30),( 22, 31)
,(-1,-16)
-- state  39
,( 3, 27),( 13, 29),( 15, 30)
,( 22, 31),(-1,-17)
-- state  40
,(-1,-18)
-- state  41
,( 13, 29)
,( 22, 31),(-1,-19)
-- state  42
,( 13, 29),(-1,-20)

-- state  43
,(-1,-12)
-- state  44
,( 2, 45),(-1,-5)
-- state  45
,( 4, 24)
,( 14, 8),(-1,-3000)
-- state  46
,(-1,-9)
-- state  47
,( 16, 48)
,(-1,-3000)
-- state  48
,(-1,-10)
);
--  The offset vector
SHIFT_REDUCE_OFFSET : array (0.. 48) of Integer :=
( 0,
 1, 5, 7, 9, 10, 11, 13, 15, 16, 18,
 20, 22, 30, 33, 34, 40, 48, 49, 50, 51,
 59, 67, 75, 77, 78, 79, 81, 89, 97, 105,
 113, 121, 128, 129, 130, 131, 132, 133, 137, 142,
 143, 146, 148, 149, 151, 154, 155, 157);
end Prop_Parser_Shift_Reduce;
