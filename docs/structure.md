single process
fixed multi task


umios lib
  scheduler
    notify
    wait_block
    systick
    context switch
  service logic
    loader
      sign validator
    updater
      relocator
      crc validator
    shell
    file system
    midi
    audio
  memory
    layout
    mpu
    heap/stack
    fault
    shared
  port
    isr

io service
  audio
  midi
    parser
      realtime
      sysex
        stdio


driver service

umios structure
  task
    audio[0]
      audio device (usb, sai)
      audio process
    system[1]
      service
        loader
        updater
        shell
        file system
        driver
          usb
          i2s
    control[2]
      control process
    idle[3]
      wfi
  memory
    layout
    mpu
    heap/stack
    fault
    shared
  port
    isr
    systick
    timer

umios api
  syscall
  shared

app format
  .umia
    header
      crc
      sign
  entry
  
