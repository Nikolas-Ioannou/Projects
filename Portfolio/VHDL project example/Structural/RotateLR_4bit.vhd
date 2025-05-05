----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 11/11/2024 05:04:55 PM
-- Design Name: 
-- Module Name: RotateLR_4bit - Behavioral
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

entity RotateLR_4bit is
    Port ( a : in STD_LOGIC_VECTOR (3 downto 0);
           Control : in STD_LOGIC;
           Result : out STD_LOGIC_VECTOR (3 downto 0));


end RotateLR_4bit;

architecture Behavioral of RotateLR_4bit is
begin
    process (a,Control)
    begin
     if Control = '0' then  -- Doing ROL
        Result <=  a(2 downto 0) & a(3);
      else -- Doing ROR
         Result <= a(0) & a(3 downto 1);
     end if;
   end process;
end Behavioral;