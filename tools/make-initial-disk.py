#!/usr/bin/env python3

import os
import sys
import struct
import argparse

IMAGE_SIZE = 32 * 1024 * 1024  # 32 MB
SECTOR_SIZE = 512
PARTITION_LBA = 63

def write_mbr(f):
    # Minimal MBR with one partition starting at LBA 63
    mbr = bytearray(512)

    # Partition entry at offset 0x1BE
    # status, CHS begin, type, CHS end, LBA start, LBA length
    total_sectors = IMAGE_SIZE // SECTOR_SIZE
    partition_sectors = total_sectors - PARTITION_LBA
    
    struct.pack_into("<B3sB3sII", mbr, 0x1BE,
                     0x80,              # Status (bootable)
                     b"\x00\x02\x00",   # CHS start
                     0x06,              # Type: FAT16
                     b"\xFF\xFF\xFF",   # CHS end (dummy)
                     PARTITION_LBA,
                     partition_sectors)

    # Signature
    mbr[510] = 0x55
    mbr[511] = 0xAA

    f.seek(0)
    f.write(mbr)

class MinimalFAT16:
    def __init__(self, partition_sectors):
        self.partition_sectors = partition_sectors
        self.sectors_per_cluster = 4 # 2KB clusters
        self.reserved_sectors = 1
        self.num_fats = 2
        self.root_entries = 512
        self.sectors_per_fat = 64
        
        self.bytes_per_cluster = self.sectors_per_cluster * SECTOR_SIZE
        
        # Initialize FAT tables (2 fats)
        self.fat = [0] * (self.sectors_per_fat * SECTOR_SIZE // 2)
        self.fat[0] = 0xFFF8 # Media descriptor
        self.fat[1] = 0xFFFF # EOF marker for cluster 1 (reserved)
        
        self.root_dir = bytearray(self.root_entries * 32)
        self.data_area = bytearray()
        self.next_free_cluster = 2
        self.root_dir_idx = 0

    def add_file(self, name, content):
        if self.root_dir_idx >= self.root_entries:
            print(f"Error: Root directory full, cannot add {name}")
            return

        # Prepare 8.3 filename
        base, ext = os.path.splitext(name)
        ext = ext.lstrip('.').upper()[:3]
        base = base.upper()[:8]
        filename_83 = f"{base:<8}{ext:<3}".encode('ascii', 'ignore')

        # Allocate clusters
        start_cluster = 0
        if len(content) > 0:
            start_cluster = self.next_free_cluster
            num_clusters = (len(content) + self.bytes_per_cluster - 1) // self.bytes_per_cluster
            
            for i in range(num_clusters):
                cluster = self.next_free_cluster + i
                if i == num_clusters - 1:
                    self.fat[cluster] = 0xFFFF # EOF
                else:
                    self.fat[cluster] = cluster + 1
            
            self.next_free_cluster += num_clusters
            
            # Pad content to cluster boundary
            padded_content = content + b"\x00" * (num_clusters * self.bytes_per_cluster - len(content))
            self.data_area += padded_content

        # Create directory entry
        entry = bytearray(32)
        entry[0:11] = filename_83
        entry[11] = 0x20 # Archive attribute
        struct.pack_into("<HI", entry, 26, start_cluster, len(content)) # Time/Date omitted for simplicity
        
        self.root_dir[self.root_dir_idx*32 : (self.root_dir_idx+1)*32] = entry
        self.root_dir_idx += 1

    def finalize(self):
        partition = bytearray()
        
        # 1. Boot Sector
        bpb = bytearray(512)
        bpb[0:3] = b"\xEB\x3C\x90"
        bpb[3:11] = b"MSWIN4.1"
        struct.pack_into("<HBHBHHBHHHII", bpb, 11,
                         SECTOR_SIZE, self.sectors_per_cluster, self.reserved_sectors,
                         self.num_fats, self.root_entries, 0, 0xF8, self.sectors_per_fat,
                         32, 64, PARTITION_LBA, self.partition_sectors)
        struct.pack_into("<BBBL11s8s", bpb, 36, 0x80, 0x00, 0x29, 0x12345678, b"NO NAME    ", b"FAT16   ")
        bpb[510] = 0x55
        bpb[511] = 0xAA
        partition += bpb
        
        # 2. FATs
        fat_bytes = b"".join(struct.pack("<H", x) for x in self.fat)
        partition += fat_bytes
        partition += fat_bytes # FAT2
        
        # 3. Root Directory
        partition += self.root_dir
        
        # 4. Data Area
        partition += self.data_area
        
        return partition

def main():
    parser = argparse.ArgumentParser(description="Create a FAT16 disk image.")
    parser.add_argument("output", help="Path to the output image file")
    parser.add_argument("src_dir", help="Source directory to copy into the image")
    args = parser.parse_args()

    # Ensure output directory exists
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    partition_sectors = (IMAGE_SIZE // SECTOR_SIZE) - PARTITION_LBA
    fat_fs = MinimalFAT16(partition_sectors)

    if os.path.exists(args.src_dir):
        for filename in sorted(os.listdir(args.src_dir)):
            filepath = os.path.join(args.src_dir, filename)
            if os.path.isfile(filepath):
                print(f"Adding {filename}...")
                with open(filepath, "rb") as f:
                    fat_fs.add_file(filename, f.read())

    print(f"Creating image {args.output}...")
    with open(args.output, "wb") as f:
        f.truncate(IMAGE_SIZE)
        write_mbr(f)
        f.seek(PARTITION_LBA * SECTOR_SIZE)
        f.write(fat_fs.finalize())
    
    print("Done.")

if __name__ == "__main__":
    main()
