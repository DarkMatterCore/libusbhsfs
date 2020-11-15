#ifdef DEBUG
    u32 bufsize = 0x8000000; /* Enough blocks to exceed the Read (10) limit. */
    if (bufsize > capacity) bufsize = capacity;
    
    char path[0x20] = {0};
    sprintf(path, "sdmc:/%d_chunk.bin", drive_ctx->usb_if_id);
    
    u8 *bigbuf = malloc(bufsize);
    FILE *fd = fopen(path, "wb");
    
    u64 cur_block_addr = 0;
    u64 data_transferred = 0;
    u32 total_block_count = (bufsize / block_length);
    u32 cmd_max_block_count = (long_lba ? (SCSI_RW10_MAX_BLOCK_COUNT + 1) : SCSI_RW10_MAX_BLOCK_COUNT);
    u32 buf_block_count = (USB_CTRL_XFER_BUFFER_SIZE / block_length);
    u32 max_block_count_per_loop = ALIGN_DOWN(cmd_max_block_count, buf_block_count);
    bool ok = false, cmd = false;
    
    if (bigbuf && fd)
    {
        time_t start = time(NULL);
        
        while(total_block_count)
        {
            u32 xfer_block_count = (total_block_count > max_block_count_per_loop ? max_block_count_per_loop : total_block_count);
            
            USBHSFS_LOG("Reading 0x%X blocks from LBA 0x%lX (interface %d, LUN %u).", xfer_block_count, cur_block_addr, drive_ctx->usb_if_id, lun);
            cmd = (long_lba ? usbHsFsScsiSendRead16Command(drive_ctx, lun, bigbuf + data_transferred, cur_block_addr, xfer_block_count, block_length, fua_supported) : \
                              usbHsFsScsiSendRead10Command(drive_ctx, lun, bigbuf + data_transferred, (u32)cur_block_addr, (u16)xfer_block_count, block_length, fua_supported));
            if (!cmd) break;
            
            data_transferred += (xfer_block_count * block_length);
            cur_block_addr += xfer_block_count;
            total_block_count -= xfer_block_count;
        }
        
        ok = (total_block_count == 0);
        
        if (ok)
        {
            time_t end = time(NULL);
            USBHSFS_LOG("Chunk dumped in %lu seconds.", end - start);
            
            fwrite(bigbuf, 1, bufsize, fd);
            fclose(fd);
            fd = NULL;
            
            cur_block_addr = 0;
            data_transferred = 0;
            total_block_count = (bufsize / block_length);
            
            start = time(NULL);
            
            while(total_block_count)
            {
                u32 xfer_block_count = (total_block_count > max_block_count_per_loop ? max_block_count_per_loop : total_block_count);
                
                USBHSFS_LOG("Writing 0x%X blocks from LBA 0x%lX (interface %d, LUN %u).", xfer_block_count, cur_block_addr, drive_ctx->usb_if_id, lun);
                cmd = (long_lba ? usbHsFsScsiSendWrite16Command(drive_ctx, lun, bigbuf + data_transferred, cur_block_addr, xfer_block_count, block_length, fua_supported) : \
                                  usbHsFsScsiSendWrite10Command(drive_ctx, lun, bigbuf + data_transferred, (u32)cur_block_addr, (u16)xfer_block_count, block_length, fua_supported));
                if (!cmd) break;
                
                data_transferred += (xfer_block_count * block_length);
                cur_block_addr += xfer_block_count;
                total_block_count -= xfer_block_count;
            }
            
            if (total_block_count == 0)
            {
                end = time(NULL);
                USBHSFS_LOG("Chunk written in %lu seconds.", end - start);
            }
        }
    }
    
    if (fd)
    {
        fclose(fd);
        if (!ok) remove(path);
    }
    
    if (bigbuf) free(bigbuf);
#endif