----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 11/11/2024 05:04:55 PM
-- Design Name: 
-- Module Name: ShiftLR_4bit - Behavioral
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

entity ShiftLR_4bit is
    Port ( a    : in STD_LOGIC_VECTOR (3 downto 0);
           Control : in STD_LOGIC_VECTOR (1 downto 0);
           Result : out STD_LOGIC_VECTOR (3 downto 0));


end ShiftLR_4bit;

architecture Behavioral of ShiftLR_4bit is
signal temp_result: STD_LOGIC_VECTOR(3 downto 0);
begin
    process (a,Control)
    begin
      if Control(1) = '0' then  -- Doing sll or sla with 2
                if a(3) = '1' then -- Overflow if I did sll
                    temp_result <= "1111"; -- Overflow case
                elsif Control(0) = '0' then -- sll
                    temp_result <= a(2 downto 0) & '0';
                else -- sla with 2
                    if a(2) = '1' then -- Overflow if i did sll 2
                        temp_result <= "1111"; -- Overflow case
                    else 
                        temp_result <= a(1 downto 0) & "00";
                    end if;
                end if;
       else  -- Doing srl or sra with 2
            if Control(0) = '0' then -- srl
                temp_result <= '0' & a(3 downto 1);  
            else -- sra with 2
                temp_result <= "00" & a(3 downto 2);  
            end if;
       end if;
       Result <= temp_result;
end process;
end Behavioral;
