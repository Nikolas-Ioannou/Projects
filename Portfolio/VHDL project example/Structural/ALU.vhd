----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 11/11/2024 05:23:28 PM
-- Design Name: 
-- Module Name: ALU - Structural
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
-- 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity ALU is
  Port ( a : in STD_LOGIC_VECTOR (3 downto 0);
           Control : in STD_LOGIC_VECTOR (2 downto 0);
           Result : out STD_LOGIC_VECTOR (3 downto 0));
end ALU;

architecture Structural of ALU is
    component ShiftLR_4bit 
        Port ( a    : in STD_LOGIC_VECTOR (3 downto 0);
               Control : in STD_LOGIC_VECTOR (1 downto 0);
               Result : out STD_LOGIC_VECTOR (3 downto 0));
    
    end component;
    component RotateLR_4bit 
        Port ( a : in STD_LOGIC_VECTOR (3 downto 0);
               Control : in STD_LOGIC;
               Result : out STD_LOGIC_VECTOR (3 downto 0));
    end component;
    signal result_shift : STD_LOGIC_VECTOR (3 downto 0);
    signal result_rotate : STD_LOGIC_VECTOR (3 downto 0);
    begin
        U1: ShiftLR_4bit port map(a => a, Control => Control(1 downto 0), Result => result_shift);
        U2: RotateLR_4bit port map(a => a, Control => Control(1), Result => result_rotate);
        process(a, Control)
        begin 
        if Control(2) = '0' then --ShiftLR
                Result <= result_shift; 
        else
            Result <= result_rotate;   
         end if;
    end process;
end Structural;