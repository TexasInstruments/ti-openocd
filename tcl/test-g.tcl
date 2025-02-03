## Using this test script for quick CI:
#../src/openocd -f board/ti_mspm0_launchpad.cfg -f ./test-g.tcl

# init before running this script - this allows us to provide this as -f option at command line.
init
mspm0_board_reset
sleep 1000
mspm0_factory_reset
sleep 1000
flash info 1
mspm0_mass_erase
flash protect 1 0 last off
flash erase_sector 1 0 last
program /home/nmenon/tmp/mspm0_images/out_of_box_LP_MSPM0G3507_nortos_ticlang.out verify
mspm0_start_bootloader
sleep 1000
mspm0_board_reset
shutdown
