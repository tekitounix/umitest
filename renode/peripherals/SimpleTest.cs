// Simple test peripheral
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class SimpleTest : IDoubleWordPeripheral, IKnownSize
    {
        public SimpleTest()
        {
        }

        public uint ReadDoubleWord(long offset)
        {
            return 0x12345678;
        }

        public void WriteDoubleWord(long offset, uint value)
        {
        }

        public void Reset()
        {
        }

        public long Size => 0x100;
    }
}
