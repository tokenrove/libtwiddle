from hypothesis import given
from test_helpers import TwiddleTest, single_set, double_set
from twiddle import BloomFilterA2

class TestBloomFilterA2(TwiddleTest):
  @given(single_set)
  def test_bloomfilter_a2(self, n_xs):
    n, xs = n_xs
    bf = BloomFilterA2(n, 8, 0.5)

    for x in xs:
      bf.set(x)
      assert(x in bf)
