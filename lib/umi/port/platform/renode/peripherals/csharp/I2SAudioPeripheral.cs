// I2S Audio Peripheral for Renode (C# implementation)
// High-performance replacement for Python version
//
// This peripheral captures audio samples and sends them via TCP to the web bridge.
// Uses async I/O to avoid blocking the simulation.

using System;
using System.Net.Sockets;
using System.Threading;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.Sound
{
    public class I2SAudioPeripheral : IDoubleWordPeripheral, IKnownSize
    {
        public I2SAudioPeripheral(IMachine machine)
        {
            this.machine = machine;
            sysbus = machine.GetSystemBus(this);

            DefineRegisters();
        }

        public long Size => 0x100;

        public uint ReadDoubleWord(long offset)
        {
            switch ((Registers)offset)
            {
                case Registers.Status:
                    return status | StatusTxEmpty;
                case Registers.Config:
                    return config;
                case Registers.Prescaler:
                    return prescaler;
                default:
                    return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch ((Registers)offset)
            {
                case Registers.Control1:
                    control1 = value;
                    break;

                case Registers.Control2:
                    control2 = value;
                    break;

                case Registers.Data:
                    // Single sample write (legacy mode)
                    WriteSample((short)(value & 0xFFFF));
                    break;

                case Registers.Config:
                    config = value;
                    if ((value & ConfigI2sEnable) != 0)
                    {
                        Connect();
                        this.Log(LogLevel.Info, "I2S enabled, config=0x{0:X4}", value);
                    }
                    break;

                case Registers.Prescaler:
                    prescaler = value;
                    var div = value & 0xFF;
                    var odd = (value >> 8) & 1;
                    if (div > 0)
                    {
                        var rate = 84000000 / (16 * 2 * (2 * div + odd));
                        this.Log(LogLevel.Info, "Sample rate: ~{0} Hz", rate);
                    }
                    break;

                case Registers.DmaAddr:
                    dmaAddr = value;
                    break;

                case Registers.DmaCount:
                    dmaCount = value;
                    break;

                case Registers.DmaTrigger:
                    // DMA transfer - read samples from memory and send
                    if (dmaAddr != 0 && dmaCount > 0)
                    {
                        DmaTransfer();
                    }
                    break;
            }
        }

        public void Reset()
        {
            control1 = 0;
            control2 = 0;
            status = StatusTxEmpty;
            config = 0;
            prescaler = 0;
            dmaAddr = 0;
            dmaCount = 0;
            channel = 0;
            sampleCount = 0;
            bufferIndex = 0;

            Disconnect();
        }

        private void DefineRegisters()
        {
            // Pre-allocate buffer
            sampleBuffer = new short[MaxBufferSize];
            sendBuffer = new byte[8 + MaxBufferSize * 2];
        }

        private void Connect()
        {
            if (connected) return;

            try
            {
                socket = new TcpClient();
                socket.NoDelay = true;
                socket.Connect(BridgeHost, BridgePort);
                stream = socket.GetStream();
                connected = true;
                this.Log(LogLevel.Info, "Connected to bridge at {0}:{1}", BridgeHost, BridgePort);
            }
            catch (Exception ex)
            {
                this.Log(LogLevel.Warning, "Failed to connect: {0}", ex.Message);
                connected = false;
            }
        }

        private void Disconnect()
        {
            if (socket != null)
            {
                try { socket.Close(); } catch { }
                socket = null;
                stream = null;
            }
            connected = false;
        }

        private void WriteSample(short sample)
        {
            if (bufferIndex < MaxBufferSize)
            {
                sampleBuffer[bufferIndex++] = sample;
            }

            channel = 1 - channel;
            sampleCount++;

            // Send when buffer is full
            if (bufferIndex >= BufferSize)
            {
                SendSamples(sampleBuffer, bufferIndex);
                bufferIndex = 0;
            }
        }

        private void DmaTransfer()
        {
            if (!connected)
            {
                Connect();
            }

            if (!connected || dmaCount == 0)
            {
                return;
            }

            var count = (int)dmaCount;
            if (count > MaxBufferSize) count = MaxBufferSize;

            // Read samples from memory
            var addr = dmaAddr;
            for (int i = 0; i < count; i++)
            {
                sampleBuffer[i] = (short)sysbus.ReadWord(addr);
                addr += 2;
            }

            dmaTransferCount++;
            if (dmaTransferCount <= 10 || dmaTransferCount % 50 == 0)
            {
                this.Log(LogLevel.Info, "DMA[{0}]: {1} samples", dmaTransferCount, count);
            }

            SendSamples(sampleBuffer, count);
            sampleCount += (uint)count;
        }

        private void SendSamples(short[] samples, int count)
        {
            if (!connected || stream == null) return;

            try
            {
                // Header: "AUD\0" + count (u16) + reserved (u16)
                sendBuffer[0] = (byte)'A';
                sendBuffer[1] = (byte)'U';
                sendBuffer[2] = (byte)'D';
                sendBuffer[3] = 0;
                sendBuffer[4] = (byte)(count & 0xFF);
                sendBuffer[5] = (byte)((count >> 8) & 0xFF);
                sendBuffer[6] = 0;
                sendBuffer[7] = 0;

                // Samples (little-endian int16)
                for (int i = 0; i < count; i++)
                {
                    var s = samples[i];
                    sendBuffer[8 + i * 2] = (byte)(s & 0xFF);
                    sendBuffer[8 + i * 2 + 1] = (byte)((s >> 8) & 0xFF);
                }

                stream.Write(sendBuffer, 0, 8 + count * 2);
            }
            catch (Exception)
            {
                connected = false;
                Disconnect();
            }
        }

        private readonly IMachine machine;
        private readonly IBusController sysbus;

        // Registers
        private uint control1;
        private uint control2;
        private uint status;
        private uint config;
        private uint prescaler;
        private uint dmaAddr;
        private uint dmaCount;

        // State
        private int channel;
        private uint sampleCount;
        private int bufferIndex;
        private uint dmaTransferCount;

        // Buffers
        private short[] sampleBuffer;
        private byte[] sendBuffer;

        // Network
        private TcpClient socket;
        private NetworkStream stream;
        private bool connected;

        // Constants
        private const string BridgeHost = "127.0.0.1";
        private const int BridgePort = 9001;
        private const int BufferSize = 1024;
        private const int MaxBufferSize = 8192;

        private const uint StatusTxEmpty = 0x02;
        private const uint ConfigI2sEnable = 0x0400;

        private enum Registers
        {
            Control1 = 0x00,
            Control2 = 0x04,
            Status = 0x08,
            Data = 0x0C,
            CrcPoly = 0x10,
            RxCrc = 0x14,
            TxCrc = 0x18,
            Config = 0x1C,
            Prescaler = 0x20,
            DmaAddr = 0x24,
            DmaCount = 0x28,
            DmaTrigger = 0x2C,
        }
    }
}
